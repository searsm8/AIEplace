/**
 * @file PositionDump.cpp
 * @brief Node-position export for the offline visualizer (TODO #16).
 *
 * Everything the renderer needs is a pure function of node positions at iteration k plus static
 * design data. So dump the positions once and render offline -- as many times, and as many ways,
 * as anyone wants -- instead of paying for a render inside the optimizer's loop and re-running an
 * hour-long placement every time the view changes.
 *
 * Layout under <output_dir>/coord_dump/ (full spec + rationale in
 * .claude/1_REVIEW/handoffs/_NEW_HANDOFF_viz_offline_tool_20260805.md §4):
 *
 *   manifest.json      text  -- dtypes, die frame, quantization box, per-generation frame index
 *   nodes_gen<N>.bin   bin   -- static per-node record (position, size, kind, net degree)
 *   frames_gen<N>.bin  bin   -- concatenated uint16 u_k position frames for generation N
 *   names_gen<N>.txt   text  -- sparse "<index> <name>" for the named nodes of generation N,
 *                               i.e. everything except the anonymous fillers (TODO #14 node-lock)
 *
 * CLAUDE CODE: format v2 adds four OPTIONAL channels, each its own file per generation and each
 * written in lockstep with frames_gen<N>.bin, so frame index i seeks identically in all of them:
 *
 *   probe_gen<N>.bin   bin   -- uint16 Nesterov PROBE positions v_k, same quantization box
 *   density_gen<N>.bin bin   -- float32 bin density rho, box-averaged to at most 256x256
 *   field_gen<N>.bin   bin   -- float32 (Ex, Ey) from the same solve, same box-average
 *   forces_gen<N>.bin  bin   -- float32 per node: wl.x, wl.y, den.x, den.y, precond_weight
 *
 * Why these four and not others: everything else a renderer wants is a pure function of positions
 * plus the netlist, so dumping it would be duplicating derivable data. These are not.
 *   - v_k is a SECOND position variable, not a function of u_k. The metrics in iterations.dat are
 *     all measured at v (TODO #32), so without it the HUD describes a placement the frame is not
 *     showing, and an iteration that moved u barely at all looks like a stall.
 *   - rho and E are the placer's OWN map. Recomputing them offline means reimplementing the
 *     sqrt(2) footprint clamp, the fixed-density cap and the filler policy in Python, where they
 *     would silently drift from what the optimizer actually used.
 *   - combineGradients() forms the total gradient IN PLACE (g -= electro), so the wirelength and
 *     density terms exist separately for the length of one expression and are unrecoverable
 *     afterwards. Capturing them there is the only chance.
 *
 * Per-iteration SCALARS still live in iterations.dat (HPWL, overflow, step length, lambda,
 * backtracking retries) and are not duplicated -- the offline tool joins on the iteration number.
 */

#include "AIEplace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <system_error>

AIEPLACE_NAMESPACE_BEGIN

namespace {

/// Kind byte in nodes_gen<N>.bin. The split mirrors the cairo renderer's layer set exactly, so a
/// ported renderer can colour straight from this byte: in particular a macro frozen by phase 2 is
/// FIXED but still carries the is_movable_macro tag, and is drawn in its own colour on purpose.
enum NodeKind : uint8_t {
    KIND_MOVABLE_STDCELL = 0,
    KIND_MOVABLE_MACRO   = 1,
    KIND_FIXED           = 2,
    KIND_IOPAD           = 3,
    KIND_FILLER          = 4,
    KIND_FROZEN_MACRO    = 5,
};

/// One record in nodes_gen<N>.bin. Positions stay float32 here: there is one static file per
/// generation (two or three per run), so its size is irrelevant next to the frame stream, and
/// exact coordinates for the nodes that never move are worth more than the bytes.
#pragma pack(push, 1)
struct StaticNodeRecord {
    float    x, y, w, h;
    uint8_t  kind;
    // CLAUDE CODE: pins on this node. Static for the life of a generation and near-free to write
    // (4 B x one file per generation), and it is the one piece of netlist the renderer would
    // otherwise have to re-parse the benchmark for. Area is deliberately NOT a field -- it is
    // exactly w*h and duplicating it invites the two to disagree.
    uint32_t net_degree;
};
#pragma pack(pop)
static_assert(sizeof(StaticNodeRecord) == 21, "nodes_gen<N>.bin record layout is a wire format");

constexpr float QUANT_MAX = 65535.0f;

/// CLAUDE CODE: cap on the density/field frames written to disk, per axis. The solver grid runs
/// to 2048x2048 (bigblue3), which is 16 MB of float32 per frame and ~2 GB over a run -- far past
/// any usable heatmap resolution, since the renderer paints at most ~2048 px across. Box-averaged
/// down by an INTEGER factor so each output cell covers a whole number of solver bins.
constexpr int VIZ_BINS_MAX = 256;

} // namespace

/**
 * @brief Open the dump for this run: create coord_dump/, fix the quantization box, start gen 0.
 *
 * Called from the constructor after createRunOutputStructure(), so output_dir exists, and after
 * the node set is final (fillers created, flat index built).
 */
void Placer::initializePositionDump()
{
    pos_dump.enabled = cfg["output"]["dump_positions"].value_or(false);
    if (!pos_dump.enabled) return;

    pos_dump.interval = std::max(1, cfg["output"]["iterations_per_dump"].value_or(20));
    pos_dump.dir = output_dir / "coord_dump";
    fs::create_directories(pos_dump.dir);

    // CLAUDE CODE: the optional v2 channels. Each defaults the way its cost/value ratio points:
    // probe and forces are what make the frames diagnostic rather than decorative, density is a
    // cheap underlay, and the field is twice density's bytes for a much narrower question.
    pos_dump.probe_enabled   = cfg["output"]["dump_probe_positions"].value_or(true);
    pos_dump.density_enabled = cfg["output"]["dump_bin_density"].value_or(true);
    pos_dump.field_enabled   = cfg["output"]["dump_field"].value_or(false);
    pos_dump.forces_enabled  = cfg["output"]["dump_forces"].value_or(true);

    // Integer block factor, so an output cell is the exact mean of a whole number of solver bins
    // and no bin is counted twice or dropped. ceil() on both, so a grid that is not a multiple of
    // VIZ_BINS_MAX gets a short final block rather than an out-of-range read.
    const int nx = grid.getBinsPerRow(), ny = grid.getBinsPerCol();
    pos_dump.viz_fx = std::max(1, (nx + VIZ_BINS_MAX - 1) / VIZ_BINS_MAX);
    pos_dump.viz_fy = std::max(1, (ny + VIZ_BINS_MAX - 1) / VIZ_BINS_MAX);
    pos_dump.viz_nx = (nx + pos_dump.viz_fx - 1) / pos_dump.viz_fx;
    pos_dump.viz_ny = (ny + pos_dump.viz_fy - 1) / pos_dump.viz_fy;

    // Quantization box: the die, inflated 2x about its centre.
    //
    // Anchoring it on the die itself would clamp any node that leaves the die -- and cells leaving
    // the die is a pathology these frames exist to SHOW, so a die-box clamp would silently erase
    // exactly the signal we are looking for. The inflation costs one bit of resolution (12
    // die-units on a 400k-wide die, still far below a pixel at any usable zoom) and buys an
    // escapee margin of half a die on every side. Anything past even that is counted rather than
    // hidden -- see the clamp counter in dumpIterationPositions().
    Box die = db.getDieArea();
    const float die_w = die.getXsize(), die_h = die.getYsize();
    pos_dump.qw  = 2.0f * die_w;
    pos_dump.qh  = 2.0f * die_h;
    pos_dump.qx0 = die.getPosBottomLeft().x - 0.5f * die_w;
    pos_dump.qy0 = die.getPosBottomLeft().y - 0.5f * die_h;

    beginPositionDumpGeneration();

    // Bytes per frame, up front. A run that is about to write 20 MB a frame should say so before
    // it fills the disk, not after -- the box this was built on sat at 96% full.
    const long long frame_nodes = (long long)db.getMovableNodes().size();
    const long long viz_bins    = (long long)pos_dump.viz_nx * pos_dump.viz_ny;
    long long bytes_per_frame = 4 * frame_nodes;
    if (pos_dump.probe_enabled)   bytes_per_frame += 4 * frame_nodes;
    if (pos_dump.density_enabled) bytes_per_frame += 4 * viz_bins;
    if (pos_dump.field_enabled)   bytes_per_frame += 8 * viz_bins;
    if (pos_dump.forces_enabled)  bytes_per_frame += 4 * FORCE_FLOATS_PER_NODE * frame_nodes;

    Logger::log_info("Position dump (TODO #16): every " + std::to_string(pos_dump.interval) +
                     " iterations -> " + pos_dump.dir.string());
    Logger::log_info("Position dump channels: u_k" +
                     std::string(pos_dump.probe_enabled   ? " +v_k"     : "") +
                     std::string(pos_dump.density_enabled ? " +density" : "") +
                     std::string(pos_dump.field_enabled   ? " +field"   : "") +
                     std::string(pos_dump.forces_enabled  ? " +forces"  : "") +
                     "  (" + std::to_string(bytes_per_frame >> 20) + " MB/frame; density " +
                     std::to_string(pos_dump.viz_nx) + "x" + std::to_string(pos_dump.viz_ny) +
                     " box-averaged from " + std::to_string(grid.getBinsPerRow()) + "x" +
                     std::to_string(grid.getBinsPerCol()) + ")");
}

/**
 * @brief Close the generation currently open and start the next one.
 *
 * Call this at every point where the node SET changes -- both of phase 2's mutations
 * (freezeMovableMacros, rebuildFillers) qualify. Writes the new generation's static-node file and
 * opens its frame stream; the caller is responsible for emitting whatever boundary frame it wants
 * afterwards.
 */
void Placer::beginPositionDumpGeneration()
{
    if (!pos_dump.enabled) return;
    if (pos_dump.frames.is_open()) pos_dump.frames.close();

    DumpGeneration gen;
    gen.id           = (int)pos_dump.generations.size();
    gen.phase        = phaseName(phase);
    gen.first_iter   = iteration;
    gen.frame_nodes  = (int)db.getMovableNodes().size();
    gen.filler_start = db.getFillerStartIndex();

    std::ofstream nodes(pos_dump.dir / ("nodes_gen" + std::to_string(gen.id) + ".bin"),
                        std::ios::binary);

    // Node names, for the offline tool's node-lock view (TODO #14). SPARSE -- "<index> <name>",
    // skipping fillers. Fillers DO carry names ("filler_0", ...), but they are generated
    // whitespace rather than design objects, so locking a view onto one is meaningless and their
    // names are pure index arithmetic. They are ~35% of the node set (181k of 512k on newblue1),
    // and that is what the skip buys.
    //
    // Written PER GENERATION rather than once, and that is the point: freezeMovableMacros() and
    // rebuildFillers() both change the node set, so index i means a different node either side of
    // the phase-2 boundary. The name is the only identifier that survives it.
    std::ofstream names(pos_dump.dir / ("names_gen" + std::to_string(gen.id) + ".txt"));

    // Index order is the contract between the two files: the frames carry the movable+filler
    // prefix, so it must come first and in getMovableNodes() order, and the nodes that never move
    // follow it. Frame slot i and static record i are then the same node by construction.
    auto emit = [&nodes, &names, &gen](Node* node_p, uint8_t kind) {
        StaticNodeRecord rec{node_p->getX(), node_p->getY(),
                             node_p->getXsize(), node_p->getYsize(), kind,
                             (uint32_t)node_p->getNets().size()};
        nodes.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        const std::string& name = node_p->getName();
        if (kind != KIND_FILLER && !name.empty())
            names << gen.num_static_nodes << ' ' << name << '\n';
        gen.num_static_nodes++;
    };

    const auto& movable = db.getMovableNodes();
    for (int i = 0; i < gen.frame_nodes; i++)
        emit(movable[i], i >= gen.filler_start   ? KIND_FILLER
                       : movable[i]->isMovableMacro() ? KIND_MOVABLE_MACRO
                                                      : KIND_MOVABLE_STDCELL);

    for (Component* comp_p : db.getFixedComponents())
        emit(comp_p, comp_p->isMovableMacro() ? KIND_FROZEN_MACRO : KIND_FIXED);

    for (Node* pad_p : db.getIOPadNodes())
        emit(pad_p, KIND_IOPAD);

    nodes.close();
    names.close();

    pos_dump.frames.open(pos_dump.dir / ("frames_gen" + std::to_string(gen.id) + ".bin"),
                           std::ios::binary);

    // CLAUDE CODE: one file per channel per generation, opened together so a frame written to one
    // is written to all and the frame index stays a valid seek key in every stream.
    auto open_channel = [&](std::ofstream& stream, bool enabled, const char* prefix) {
        if (stream.is_open()) stream.close();
        if (enabled)
            stream.open(pos_dump.dir / (prefix + std::to_string(gen.id) + ".bin"),
                        std::ios::binary);
    };
    open_channel(pos_dump.probe,   pos_dump.probe_enabled,   "probe_gen");
    open_channel(pos_dump.density, pos_dump.density_enabled, "density_gen");
    open_channel(pos_dump.field,   pos_dump.field_enabled,   "field_gen");
    open_channel(pos_dump.forces,  pos_dump.forces_enabled,  "forces_gen");

    // The capture buffer is indexed by the same i as the frame, so a node-set change resizes it.
    // Both auxiliary channels are stale until their producer runs again at the new positions:
    // freezeMovableMacros/legalizeMacros moved nodes with no re-solve, and rebuildFillers made
    // node i a different node. Marking them stale here is what puts the 0 bits in frame_valid.
    if (pos_dump.forces_enabled)
        pos_dump.force_capture.assign((size_t)gen.frame_nodes * FORCE_FLOATS_PER_NODE, 0.0f);
    pos_dump.density_fresh = false;
    pos_dump.forces_fresh  = false;

    pos_dump.generations.push_back(std::move(gen));
}

/**
 * @brief Write one position frame.
 *
 * Cadence-gated exactly like the renderer it replaces, with one exception: a frame carrying a
 * @p tag is forced through regardless of the cadence. That is for the phase-1 -> phase-2
 * transition, where the legalization jump and the standard-cell re-seed both happen inside a
 * single iteration that the regular cadence can miss by up to `interval` frames on either side.
 * The tag is recorded in the manifest so the offline tool can caption those frames.
 */
void Placer::dumpIterationPositions(const std::string& tag)
{
    if (!pos_dump.enabled) return;
    if (tag.empty() && iteration > 1 && iteration % pos_dump.interval != 0) return;

    DumpGeneration& gen = pos_dump.generations.back();
    const auto& movable = db.getMovableNodes();

    // A node-count change without a matching beginPositionDumpGeneration() would desync every
    // subsequent frame from the static records and render as garbage rather than as an error.
    // Refuse the frame instead, and say so.
    if ((int)movable.size() != gen.frame_nodes) {
        Logger::log_error("Position dump: node count changed from " +
                          std::to_string(gen.frame_nodes) + " to " +
                          std::to_string(movable.size()) + " without a new generation; "
                          "frame at iteration " + std::to_string(iteration) + " dropped.");
        return;
    }

    // Quantize to uint16 over the inflated die box (see initializePositionDump). Staged in one
    // contiguous buffer and written once: at MMS node counts a per-node ofstream::write is the
    // difference between milliseconds and seconds per frame.
    std::vector<uint16_t> buffer(2 * (size_t)gen.frame_nodes);
    const float scale_x = QUANT_MAX / pos_dump.qw;
    const float scale_y = QUANT_MAX / pos_dump.qh;

    // CLAUDE CODE: quantize one position list into `buffer`. A lambda because the probe channel
    // runs it a second time over v_k, and the two streams MUST share the quantization box -- a
    // renderer that draws u and v in one frame is comparing them directly.
    // (The locals are qx/qy, not u/v: since v2 this file carries the actual Nesterov u_k and v_k,
    // so reusing those two letters for "quantized x, quantized y" no longer reads.)
    auto quantize = [&](bool probe) {
        for (int i = 0; i < gen.frame_nodes; i++) {
            const Position pos = probe ? movable[i]->getProbePos() : movable[i]->getPos();
            const float qx = (pos.x - pos_dump.qx0) * scale_x;
            const float qy = (pos.y - pos_dump.qy0) * scale_y;
            // Counted on the committed positions only, so the number keeps the meaning it had in
            // v1 and stays comparable with runs made before the probe channel existed.
            if (!probe && (qx < 0.0f || qx > QUANT_MAX || qy < 0.0f || qy > QUANT_MAX))
                gen.clamped++;
            buffer[2 * i]     = (uint16_t)std::lround(std::clamp(qx, 0.0f, QUANT_MAX));
            buffer[2 * i + 1] = (uint16_t)std::lround(std::clamp(qy, 0.0f, QUANT_MAX));
        }
    };
    auto flush = [&](std::ofstream& stream) {
        stream.write(reinterpret_cast<const char*>(buffer.data()),
                     (std::streamsize)(buffer.size() * sizeof(uint16_t)));
    };

    quantize(false);
    flush(pos_dump.frames);
    if (pos_dump.probe_enabled) { quantize(true); flush(pos_dump.probe); }

    // The auxiliary channels are written on EVERY frame whether or not they are fresh, so that
    // frame index i stays a valid seek key in every stream; the validity bits below are what tell
    // a renderer that a given frame's record is zero-filled rather than measured.
    const bool density_valid = pos_dump.density_enabled && pos_dump.density_fresh;
    const bool forces_valid  = pos_dump.forces_enabled  && pos_dump.forces_fresh;
    if (pos_dump.density_enabled) writeDumpDensityFrame(density_valid);
    if (pos_dump.forces_enabled)  writeDumpForceFrame(gen.frame_nodes, forces_valid);

    gen.frame_iters.push_back(iteration);
    gen.frame_tags.push_back(tag);
    gen.frame_valid.push_back((density_valid ? 1 : 0) | (forces_valid ? 2 : 0));
}

/**
 * @brief CLAUDE CODE: write one box-averaged density frame, and the field frame beside it.
 *
 * Reads the SOLVER'S OWN bins -- Bin::total_overlap as deposited by computeOverlaps() and
 * Bin::eField as solved by the DCT -- rather than recomputing anything. That is the point: the
 * deposit carries the sqrt(2) footprint clamp, the area-conserving weight and the fixed-density
 * cap, three things that have each been a source of subtle mismatch, and a Python reimplementation
 * would drift from what the optimizer actually used without anyone noticing.
 *
 * Both maps describe the PROBE positions v_k, since that is where computeElectricFields deposits.
 * The committed frame in frames_gen<N>.bin is u_k, which is one more reason the probe channel
 * exists: overlaying this heatmap on the u frame compares two different placements.
 *
 * @param valid false when the grid no longer describes the placement being framed; the record is
 *              zero-filled and the frame's validity bit is cleared.
 */
void Placer::writeDumpDensityFrame(bool valid)
{
    const int nx = grid.getBinsPerRow(), ny = grid.getBinsPerCol();
    const int out_nx = pos_dump.viz_nx, out_ny = pos_dump.viz_ny;
    const int fx = pos_dump.viz_fx, fy = pos_dump.viz_fy;
    const float bin_area_inv = 1.0f / (grid.getBinWidth() * grid.getBinHeight());

    // Row-major with y as the row, matching dumpBinDensity()'s CSV so the two are comparable.
    std::vector<float> rho((size_t)out_nx * out_ny, 0.0f);
    std::vector<float> efield;
    if (pos_dump.field_enabled) efield.assign(2 * (size_t)out_nx * out_ny, 0.0f);

    if (valid) {
        for (int oy = 0; oy < out_ny; oy++) {
            const int row_lo = oy * fy, row_hi = std::min(ny, row_lo + fy);
            for (int ox = 0; ox < out_nx; ox++) {
                const int col_lo = ox * fx, col_hi = std::min(nx, col_lo + fx);
                float sum_area = 0.0f, sum_ex = 0.0f, sum_ey = 0.0f;
                for (int col = col_lo; col < col_hi; col++)
                    for (int row = row_lo; row < row_hi; row++) {
                        // Grid::m_bins is indexed [col][row] -- see Grid::getBinDensities.
                        Bin& bin = grid.getBin(col, row);
                        sum_area += bin.total_overlap;
                        sum_ex   += bin.eField.x;
                        sum_ey   += bin.eField.y;
                    }
                // Divide by the block's ACTUAL member count, not fx*fy: a grid that is not a
                // multiple of VIZ_BINS_MAX has a short final block, and the nominal factor would
                // dim exactly the top and right edges of the map.
                const float inv_count = 1.0f / (float)((col_hi - col_lo) * (row_hi - row_lo));
                const size_t o = (size_t)oy * out_nx + ox;
                rho[o] = sum_area * bin_area_inv * inv_count;
                if (!efield.empty()) {
                    efield[2 * o]     = sum_ex * inv_count;
                    efield[2 * o + 1] = sum_ey * inv_count;
                }
            }
        }
    }

    pos_dump.density.write(reinterpret_cast<const char*>(rho.data()),
                             (std::streamsize)(rho.size() * sizeof(float)));
    if (pos_dump.field_enabled)
        pos_dump.field.write(reinterpret_cast<const char*>(efield.data()),
                               (std::streamsize)(efield.size() * sizeof(float)));
}

/**
 * @brief CLAUDE CODE: write one per-node force frame from the buffer combineGradients() filled.
 *
 * No computation here. The wirelength/density split exists only inside combineGradients(), which
 * forms the total gradient in place, so the capture has to happen there and this just spills it.
 *
 * @param valid false when no combineGradients() has run at the framed positions; zero-filled.
 */
void Placer::writeDumpForceFrame(int frame_nodes, bool valid)
{
    const size_t floats = (size_t)frame_nodes * FORCE_FLOATS_PER_NODE;
    if (pos_dump.force_capture.size() != floats)
        pos_dump.force_capture.assign(floats, 0.0f);
    else if (!valid)
        std::fill(pos_dump.force_capture.begin(), pos_dump.force_capture.end(), 0.0f);

    pos_dump.forces.write(reinterpret_cast<const char*>(pos_dump.force_capture.data()),
                            (std::streamsize)(floats * sizeof(float)));
}

/// @brief Close the frame stream and write manifest.json — the file that makes the binaries
///        interpretable. Called once, at the end of the run.
void Placer::finalizePositionDump()
{
    if (!pos_dump.enabled) return;
    for (std::ofstream* stream : {&pos_dump.frames, &pos_dump.probe, &pos_dump.density,
                                  &pos_dump.field, &pos_dump.forces})
        if (stream->is_open()) stream->close();

    Box die = db.getDieArea();
    std::ofstream manifest(pos_dump.dir / "manifest.json");
    manifest << std::setprecision(9);

    manifest << "{\n"
             << "  \"format_version\": 2,\n"
             << "  \"benchmark\": \"" << db.getBenchmarkName() << "\",\n"
             << "  \"die\": {\"x0\": " << die.getPosBottomLeft().x
             << ", \"y0\": " << die.getPosBottomLeft().y
             << ", \"w\": " << die.getXsize() << ", \"h\": " << die.getYsize() << "},\n"
             // Every coordinate in this dump is in the placer's INTERNAL frame, whose origin is
             // the die's lower-left. Bookshelf inputs are translated into it at parse time
             // (DataBase::bookshelf_end), so add die_shift back to recover benchmark/DEF
             // coordinates -- on the MMS suite that is a real 459-unit offset, not zero.
             << "  \"die_shift\": {\"x\": " << db.getDieShift().x
             << ", \"y\": " << db.getDieShift().y << "},\n"
             << "  \"row_height\": " << db.getRowHeight() << ",\n"
             << "  \"bins_per_row\": " << bins_per_row << ",\n"
             << "  \"target_density\": " << target_density << ",\n"
             << "  \"export_interval\": " << pos_dump.interval << ",\n"
             // Frames decode as  pos = q0 + (u / max) * qsize.
             << "  \"quant\": {\"x0\": " << pos_dump.qx0 << ", \"y0\": " << pos_dump.qy0
             << ", \"w\": " << pos_dump.qw << ", \"h\": " << pos_dump.qh
             << ", \"max\": " << (int)QUANT_MAX << "},\n"
             << "  \"static_record\": [\"f4 x\", \"f4 y\", \"f4 w\", \"f4 h\", \"u1 kind\", "
                "\"u4 net_degree\"],\n"
             << "  \"kinds\": {\"0\": \"movable_stdcell\", \"1\": \"movable_macro\", "
                "\"2\": \"fixed\", \"3\": \"iopad\", \"4\": \"filler\", \"5\": \"frozen_macro\"},\n"
             // CLAUDE CODE: which optional channels this run wrote, and how to decode each. A
             // reader should key off THIS rather than probing for files, so that "channel was
             // disabled" and "channel is stale on this frame" stay distinguishable -- the second
             // is frame_valid, and only a per-frame flag can express it.
             << "  \"channels\": {\n"
             << "    \"frames\":  {\"file\": \"frames_gen<N>.bin\", \"per\": \"frame_node\", "
                "\"record\": [\"u2 x\", \"u2 y\"], \"position\": \"u_k committed\"},\n"
             << "    \"probe\":   {\"present\": " << (pos_dump.probe_enabled ? "true" : "false")
             << ", \"file\": \"probe_gen<N>.bin\", \"per\": \"frame_node\", "
                "\"record\": [\"u2 x\", \"u2 y\"], \"position\": \"v_k Nesterov probe\"},\n"
             << "    \"density\": {\"present\": " << (pos_dump.density_enabled ? "true" : "false")
             << ", \"file\": \"density_gen<N>.bin\", \"per\": \"viz_bin\", "
                "\"record\": [\"f4 rho\"], \"valid_bit\": 1, \"at\": \"v_k\"},\n"
             << "    \"field\":   {\"present\": " << (pos_dump.field_enabled ? "true" : "false")
             << ", \"file\": \"field_gen<N>.bin\", \"per\": \"viz_bin\", "
                "\"record\": [\"f4 Ex\", \"f4 Ey\"], \"valid_bit\": 1, \"at\": \"v_k\"},\n"
             << "    \"forces\":  {\"present\": " << (pos_dump.forces_enabled ? "true" : "false")
             << ", \"file\": \"forces_gen<N>.bin\", \"per\": \"frame_node\", "
                "\"record\": [\"f4 wl_x\", \"f4 wl_y\", \"f4 den_x\", \"f4 den_y\", "
                "\"f4 precond\"], \"valid_bit\": 2, \"at\": \"v_k\"}\n"
             << "  },\n"
             // The density/field maps are box-averaged from the solver grid by an integer factor;
             // viz_bin (bx, by) covers solver bins [bx*factor_x, (bx+1)*factor_x) x likewise in y,
             // clipped at the grid edge. Stored row-major with y as the row.
             << "  \"viz_grid\": {\"nx\": " << pos_dump.viz_nx
             << ", \"ny\": " << pos_dump.viz_ny
             << ", \"factor_x\": " << pos_dump.viz_fx
             << ", \"factor_y\": " << pos_dump.viz_fy
             << ", \"solver_nx\": " << grid.getBinsPerRow()
             << ", \"solver_ny\": " << grid.getBinsPerCol() << "},\n"
             // frame_valid is a per-frame bitmask over the auxiliary channels: bit 0 (=1) the
             // density/field record was measured at this frame's placement, bit 1 (=2) the force
             // record was. A cleared bit means the record is zero-filled -- see frame_valid in
             // AIEplace.h for which frames break which channel and why.
             << "  \"frame_valid_bits\": {\"1\": \"density,field\", \"2\": \"forces\"},\n"
             << "  \"generations\": [\n";

    for (size_t g = 0; g < pos_dump.generations.size(); g++) {
        const DumpGeneration& gen = pos_dump.generations[g];
        manifest << "    {\"id\": " << gen.id
                 << ", \"phase\": \"" << gen.phase << "\""
                 << ", \"first_iter\": " << gen.first_iter
                 << ", \"names\": \"names_gen" << gen.id << ".txt\""
                 << ", \"num_static_nodes\": " << gen.num_static_nodes
                 << ", \"frame_nodes\": " << gen.frame_nodes
                 << ", \"filler_start\": " << gen.filler_start
                 << ", \"frames\": " << gen.frame_iters.size()
                 << ", \"clamped\": " << gen.clamped
                 << ", \"frame_iters\": [";
        for (size_t i = 0; i < gen.frame_iters.size(); i++)
            manifest << (i ? ", " : "") << gen.frame_iters[i];
        manifest << "], \"frame_tags\": [";
        for (size_t i = 0; i < gen.frame_tags.size(); i++)
            manifest << (i ? ", " : "") << "\"" << gen.frame_tags[i] << "\"";
        manifest << "], \"frame_valid\": [";
        for (size_t i = 0; i < gen.frame_valid.size(); i++)
            manifest << (i ? ", " : "") << gen.frame_valid[i];
        manifest << "]}" << (g + 1 < pos_dump.generations.size() ? "," : "") << "\n";
    }

    manifest << "  ]\n}\n";
    manifest.close();

    // CLAUDE CODE: every channel is indexed by frame number, so a stream of the wrong length does
    // not fail -- it silently returns a neighbouring frame, or half of two. Check the lengths the
    // manifest implies rather than trusting that the writes happened, and say which file is wrong.
    auto check_stream = [this](const std::string& name, bool present, long long expect) {
        if (!present) return;
        std::error_code ec;
        const long long actual = (long long)fs::file_size(pos_dump.dir / name, ec);
        if (ec)
            Logger::log_error("Position dump: cannot stat " + name + " (" + ec.message() + ").");
        else if (actual != expect)
            Logger::log_error("Position dump: " + name + " is " + std::to_string(actual) +
                              " bytes but the manifest implies " + std::to_string(expect) +
                              " -- this channel is out of lockstep with frames_gen<N>.bin, so a "
                              "frame index into it reads the wrong frame.");
    };

    long long total_frames = 0, total_clamped = 0;
    for (const DumpGeneration& gen : pos_dump.generations) {
        total_frames  += (long long)gen.frame_iters.size();
        total_clamped += gen.clamped;

        const std::string id = std::to_string(gen.id) + ".bin";
        const long long frames    = (long long)gen.frame_iters.size();
        const long long pos_bytes = frames * gen.frame_nodes * 2 * (long long)sizeof(uint16_t);
        const long long viz_bins  = (long long)pos_dump.viz_nx * pos_dump.viz_ny;
        check_stream("nodes_gen"   + id, true, (long long)gen.num_static_nodes * sizeof(StaticNodeRecord));
        check_stream("frames_gen"  + id, true, pos_bytes);
        check_stream("probe_gen"   + id, pos_dump.probe_enabled,   pos_bytes);
        check_stream("density_gen" + id, pos_dump.density_enabled, frames * viz_bins * 4);
        check_stream("field_gen"   + id, pos_dump.field_enabled,   frames * viz_bins * 8);
        check_stream("forces_gen"  + id, pos_dump.forces_enabled,
                     frames * gen.frame_nodes * FORCE_FLOATS_PER_NODE * 4);
    }

    Logger::log_info("Position dump: " + std::to_string(total_frames) + " frames in " +
                     std::to_string(pos_dump.generations.size()) + " generation(s) -> " +
                     pos_dump.dir.string());

    // Not a formatting detail: a clamped position is a node more than half a die outside the die
    // area, which is a divergence signature worth seeing rather than a rounding nuisance.
    if (total_clamped > 0)
        Logger::log_warning("Position dump: " + std::to_string(total_clamped) +
                            " positions fell outside the quantization box and were clamped "
                            "(nodes more than half a die outside the die area).");
}

AIEPLACE_NAMESPACE_END

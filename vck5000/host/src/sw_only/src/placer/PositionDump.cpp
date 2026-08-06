/**
 * @file PositionDump.cpp
 * @brief Node-position export for the offline visualizer (TODO #16).
 *
 * Everything the renderer needs is a pure function of node positions at iteration k plus static
 * design data. So dump the positions once and render offline -- as many times, and as many ways,
 * as anyone wants -- instead of paying for a render inside the optimizer's loop and re-running an
 * hour-long placement every time the view changes.
 *
 * Layout under <output_dir>/viz/ (full spec + rationale in
 * 1_REVIEW/handoffs/NEW_HANDOFF_viz_offline_tool_20260805.md §4):
 *
 *   manifest.json      text  -- dtypes, die frame, quantization box, per-generation frame index
 *   nodes_gen<N>.bin   bin   -- static per-node record (position, size, kind) for generation N
 *   frames_gen<N>.bin  bin   -- concatenated uint16 position frames for generation N
 *
 * Only POSITIONS live here. Every per-iteration scalar the overlay prints (HPWL, overflow, step
 * length, lambda) is already in iterations.dat and is not duplicated -- the offline tool reads
 * that file and joins on the iteration number.
 */

#include "AIEplace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>

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
    float   x, y, w, h;
    uint8_t kind;
};
#pragma pack(pop)
static_assert(sizeof(StaticNodeRecord) == 17, "nodes_gen<N>.bin record layout is a wire format");

constexpr float QUANT_MAX = 65535.0f;

} // namespace

/**
 * @brief Open the dump for this run: create viz/, fix the quantization box, start generation 0.
 *
 * Called from the constructor after createRunOutputStructure(), so output_dir exists, and after
 * the node set is final (fillers created, flat index built).
 */
void Placer::initializePositionDump()
{
    m_pos_dump.enabled = cfg["output"]["dump_positions"].value_or(false);
    if (!m_pos_dump.enabled) return;

    m_pos_dump.interval = std::max(1, cfg["output"]["iterations_per_dump"].value_or(20));
    m_pos_dump.dir = output_dir / "viz";
    fs::create_directories(m_pos_dump.dir);

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
    m_pos_dump.qw  = 2.0f * die_w;
    m_pos_dump.qh  = 2.0f * die_h;
    m_pos_dump.qx0 = die.getPosBottomLeft().x - 0.5f * die_w;
    m_pos_dump.qy0 = die.getPosBottomLeft().y - 0.5f * die_h;

    beginPositionDumpGeneration();

    Logger::log_info("Position dump (TODO #16): every " + std::to_string(m_pos_dump.interval) +
                     " iterations -> " + m_pos_dump.dir.string());
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
    if (!m_pos_dump.enabled) return;
    if (m_pos_dump.frames.is_open()) m_pos_dump.frames.close();

    DumpGeneration gen;
    gen.id           = (int)m_pos_dump.generations.size();
    gen.phase        = phaseName(m_phase);
    gen.first_iter   = iteration;
    gen.frame_nodes  = (int)db.getMovableNodes().size();
    gen.filler_start = db.getFillerStartIndex();

    std::ofstream nodes(m_pos_dump.dir / ("nodes_gen" + std::to_string(gen.id) + ".bin"),
                        std::ios::binary);

    // Index order is the contract between the two files: the frames carry the movable+filler
    // prefix, so it must come first and in getMovableNodes() order, and the nodes that never move
    // follow it. Frame slot i and static record i are then the same node by construction.
    auto emit = [&nodes, &gen](Node* node_p, uint8_t kind) {
        StaticNodeRecord rec{node_p->getX(), node_p->getY(),
                             node_p->getXsize(), node_p->getYsize(), kind};
        nodes.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
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

    m_pos_dump.frames.open(m_pos_dump.dir / ("frames_gen" + std::to_string(gen.id) + ".bin"),
                           std::ios::binary);
    m_pos_dump.generations.push_back(std::move(gen));
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
    if (!m_pos_dump.enabled) return;
    if (tag.empty() && iteration > 1 && iteration % m_pos_dump.interval != 0) return;

    DumpGeneration& gen = m_pos_dump.generations.back();
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
    const float scale_x = QUANT_MAX / m_pos_dump.qw;
    const float scale_y = QUANT_MAX / m_pos_dump.qh;

    for (int i = 0; i < gen.frame_nodes; i++) {
        const float u = (movable[i]->getX() - m_pos_dump.qx0) * scale_x;
        const float v = (movable[i]->getY() - m_pos_dump.qy0) * scale_y;
        if (u < 0.0f || u > QUANT_MAX || v < 0.0f || v > QUANT_MAX) gen.clamped++;
        buffer[2 * i]     = (uint16_t)std::lround(std::clamp(u, 0.0f, QUANT_MAX));
        buffer[2 * i + 1] = (uint16_t)std::lround(std::clamp(v, 0.0f, QUANT_MAX));
    }

    m_pos_dump.frames.write(reinterpret_cast<const char*>(buffer.data()),
                            (std::streamsize)(buffer.size() * sizeof(uint16_t)));

    gen.frame_iters.push_back(iteration);
    gen.frame_tags.push_back(tag);
}

/// @brief Close the frame stream and write manifest.json — the file that makes the binaries
///        interpretable. Called once, at the end of the run.
void Placer::finalizePositionDump()
{
    if (!m_pos_dump.enabled) return;
    if (m_pos_dump.frames.is_open()) m_pos_dump.frames.close();

    Box die = db.getDieArea();
    std::ofstream manifest(m_pos_dump.dir / "manifest.json");
    manifest << std::setprecision(9);

    manifest << "{\n"
             << "  \"format_version\": 1,\n"
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
             << "  \"export_interval\": " << m_pos_dump.interval << ",\n"
             // Frames decode as  pos = q0 + (u / max) * qsize.
             << "  \"quant\": {\"x0\": " << m_pos_dump.qx0 << ", \"y0\": " << m_pos_dump.qy0
             << ", \"w\": " << m_pos_dump.qw << ", \"h\": " << m_pos_dump.qh
             << ", \"max\": " << (int)QUANT_MAX << "},\n"
             << "  \"static_record\": [\"f4 x\", \"f4 y\", \"f4 w\", \"f4 h\", \"u1 kind\"],\n"
             << "  \"kinds\": {\"0\": \"movable_stdcell\", \"1\": \"movable_macro\", "
                "\"2\": \"fixed\", \"3\": \"iopad\", \"4\": \"filler\", \"5\": \"frozen_macro\"},\n"
             << "  \"generations\": [\n";

    for (size_t g = 0; g < m_pos_dump.generations.size(); g++) {
        const DumpGeneration& gen = m_pos_dump.generations[g];
        manifest << "    {\"id\": " << gen.id
                 << ", \"phase\": \"" << gen.phase << "\""
                 << ", \"first_iter\": " << gen.first_iter
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
        manifest << "]}" << (g + 1 < m_pos_dump.generations.size() ? "," : "") << "\n";
    }

    manifest << "  ]\n}\n";
    manifest.close();

    long long total_frames = 0, total_clamped = 0;
    for (const DumpGeneration& gen : m_pos_dump.generations) {
        total_frames  += (long long)gen.frame_iters.size();
        total_clamped += gen.clamped;
    }

    Logger::log_info("Position dump: " + std::to_string(total_frames) + " frames in " +
                     std::to_string(m_pos_dump.generations.size()) + " generation(s) -> " +
                     m_pos_dump.dir.string());

    // Not a formatting detail: a clamped position is a node more than half a die outside the die
    // area, which is a divergence signature worth seeing rather than a rounding nuisance.
    if (total_clamped > 0)
        Logger::log_warning("Position dump: " + std::to_string(total_clamped) +
                            " positions fell outside the quantization box and were clamped "
                            "(nodes more than half a die outside the die area).");
}

AIEPLACE_NAMESPACE_END

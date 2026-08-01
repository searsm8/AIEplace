/**
 * @file MacroLegalize.cpp
 * @brief LP-based macro legalization — stage 2 of the mixed-size flow (TODO #13).
 *
 * Port of XPlace's `src/core/macro_legalization.py`. A full walkthrough of the algorithm lives in
 * `1_REVIEW/NEW_EXPLAINER_lp_macro_legalization.md`; the short version:
 *
 *   Non-overlap of two rectangles is a DISJUNCTION (left OR right OR below OR above), which is
 *   non-convex and would normally force a MILP with O(n^2) binaries. The trick is to decide, for
 *   every pair, WHICH separation to enforce — after which each disjunction collapses to a single
 *   linear inequality and the whole problem is a plain LP.
 *
 *   1. Build a constraint graph: for each overlapping pair pick the axis needing the smaller push,
 *      oriented by the macros' current order. Edges live in G_x or G_y, never both.
 *   2. Repair it. Longest path from a source through G_x gives each macro's earliest feasible x
 *      (this is literally static timing analysis: L = arrival, R = required, slack = R - L).
 *      Negative slack means a chain is wider than the die, so move edges to the other axis.
 *   3. Solve for minimum total displacement subject to the now-fixed constraints.
 *
 * Step 2 alone already yields a legal placement (the L values satisfy every constraint), which is
 * the fallback when no LP solver is available. Step 3 is what makes it minimum-displacement.
 *
 * Scale: 91 macros on newblue5, 76 on adaptec5 — a few hundred variables. Milliseconds.
 */

#include "AIEplace.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

AIEPLACE_NAMESPACE_BEGIN

namespace {

/// One macro as the legalizer sees it: centre position, size, and whether it may move.
struct MacroBox {
    Node* node_p = nullptr;
    double cx = 0, cy = 0;      // centre (the LP's variable; sw_only stores lower-left)
    double w = 0,  h = 0;
    bool   fixed = false;
};

/// Which axis a pair's separation is enforced on. NONE = the pair needs no constraint.
enum class EdgeAxis { NONE, X, Y };

/**
 * @brief Step 1 — choose a separation axis and direction for every pair.
 *
 * `edge[i][j] == X` means "enforce cx_i + (w_i+w_j)/2 <= cx_j", i.e. i is pinned left of j.
 * At most one of edge[i][j] / edge[j][i] is set, so the graph stays a DAG.
 *
 * Mirrors XPlace constraint_graph_construction (macro_legalization.py:55). The pruning rule
 * matters as much as the choice: two boxes with no overlap in their y-projection can never
 * collide horizontally, so an x-edge between them is pure constraint bloat.
 */
std::vector<EdgeAxis> buildConstraintGraph(const std::vector<MacroBox>& macros)
{
    const int n = (int)macros.size();
    std::vector<EdgeAxis> edge(n * n, EdgeAxis::NONE);

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const MacroBox& a = macros[i];
            const MacroBox& b = macros[j];
            bool x_forward = (a.cx <= b.cx);   // i left of j?
            bool y_forward = (a.cy <= b.cy);

            // Gap along each axis at the current positions. Negative = projections overlap.
            double dist_x = x_forward ? (b.cx - b.w / 2) - (a.cx + a.w / 2)
                                      : (a.cx - a.w / 2) - (b.cx + b.w / 2);
            double dist_y = y_forward ? (b.cy - b.h / 2) - (a.cy + a.h / 2)
                                      : (a.cy - a.h / 2) - (b.cy + b.h / 2);

            EdgeAxis axis;
            if (dist_x >= 0 && dist_y >= 0)       axis = (dist_x >= dist_y) ? EdgeAxis::X : EdgeAxis::Y;
            else if (dist_x >= 0)                 axis = EdgeAxis::X;   // only x separates them
            else if (dist_y >= 0)                 axis = EdgeAxis::Y;   // only y separates them
            else                                  axis = (dist_x >= dist_y) ? EdgeAxis::X : EdgeAxis::Y;

            // Prune: no projection overlap on the other axis => they cannot collide this way.
            bool y_proj_overlap = (a.cy - a.h / 2 <= b.cy + b.h / 2) &&
                                  (b.cy - b.h / 2 <= a.cy + a.h / 2);
            bool x_proj_overlap = (a.cx - a.w / 2 <= b.cx + b.w / 2) &&
                                  (b.cx - b.w / 2 <= a.cx + a.w / 2);
            if (axis == EdgeAxis::X && !y_proj_overlap) axis = EdgeAxis::NONE;
            if (axis == EdgeAxis::Y && !x_proj_overlap) axis = EdgeAxis::NONE;
            if (axis == EdgeAxis::NONE) continue;

            // Orient the edge along the macros' current order so the graph agrees with where
            // things already are — that is what keeps displacement small.
            bool forward = (axis == EdgeAxis::X) ? x_forward : y_forward;
            if (forward) edge[i * n + j] = axis;
            else         edge[j * n + i] = axis;
        }
    return edge;
}

/// Minimum centre-to-centre separation for an edge on the given axis.
inline double edgeWeight(const MacroBox& a, const MacroBox& b, EdgeAxis axis)
{
    return (axis == EdgeAxis::X) ? (a.w + b.w) / 2 : (a.h + b.h) / 2;
}

/**
 * @brief Longest-path pass over one axis' DAG: the earliest feasible centre for every macro.
 *
 * This is the ASAP/arrival-time sweep of static timing analysis. A node's earliest position is
 * the max over its predecessors of (their earliest + the required separation) — max, not min,
 * because the binding constraint in a chain is the worst one. Fixed macros pin to their actual
 * position. Returns false if the graph has a cycle (should not happen: edges follow position
 * order) or if a chain runs past the die edge, i.e. the direction assignment is infeasible.
 */
bool longestPathEarliest(const std::vector<MacroBox>& macros, const std::vector<EdgeAxis>& edge,
                         EdgeAxis axis, double die_extent, std::vector<double>& earliest)
{
    const int n = (int)macros.size();
    earliest.assign(n, 0.0);
    for (int i = 0; i < n; i++) {
        const MacroBox& m = macros[i];
        double half = (axis == EdgeAxis::X) ? m.w / 2 : m.h / 2;
        earliest[i] = m.fixed ? ((axis == EdgeAxis::X) ? m.cx : m.cy) : half;
    }

    // Kahn topological order over this axis' edges.
    std::vector<int> indeg(n, 0);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (edge[i * n + j] == axis) indeg[j]++;

    std::vector<int> order;
    order.reserve(n);
    for (int i = 0; i < n; i++) if (indeg[i] == 0) order.push_back(i);
    for (size_t k = 0; k < order.size(); k++) {
        int u = order[k];
        for (int v = 0; v < n; v++) {
            if (edge[u * n + v] != axis) continue;
            double need = earliest[u] + edgeWeight(macros[u], macros[v], axis);
            if (!macros[v].fixed) earliest[v] = std::max(earliest[v], need);
            if (--indeg[v] == 0) order.push_back(v);
        }
    }
    if ((int)order.size() != n) return false;   // cycle

    for (int i = 0; i < n; i++) {
        double half = (axis == EdgeAxis::X) ? macros[i].w / 2 : macros[i].h / 2;
        if (earliest[i] > die_extent - half + 1e-6) return false;   // chain wider than the die
    }
    return true;
}

/// Exact overlap area between two boxes at their current centres (0 if disjoint).
double overlapArea(const MacroBox& a, const MacroBox& b)
{
    double ox = std::min(a.cx + a.w / 2, b.cx + b.w / 2) - std::max(a.cx - a.w / 2, b.cx - b.w / 2);
    double oy = std::min(a.cy + a.h / 2, b.cy + b.h / 2) - std::max(a.cy - a.h / 2, b.cy - b.h / 2);
    return (ox > 0 && oy > 0) ? ox * oy : 0.0;
}

int countOverlaps(const std::vector<MacroBox>& macros, double* worst_area)
{
    int pairs = 0;
    double worst = 0.0;
    for (size_t i = 0; i < macros.size(); i++)
        for (size_t j = i + 1; j < macros.size(); j++) {
            double a = overlapArea(macros[i], macros[j]);
            if (a > 1e-6) { pairs++; worst = std::max(worst, a); }
        }
    if (worst_area) *worst_area = worst;
    return pairs;
}

/// Locate a CBC binary: explicit config path, then $PATH, then the copy pulp ships in anaconda3.
std::string findCbc(const std::string& configured)
{
    auto usable = [](const std::string& p) { return !p.empty() && access(p.c_str(), X_OK) == 0; };
    if (!configured.empty()) return usable(configured) ? configured : std::string();

    const char* home = std::getenv("HOME");
    std::vector<std::string> candidates = { "/usr/bin/cbc", "/usr/local/bin/cbc" };
    if (home) {
        std::string h(home);
        // pulp vendors a self-contained CBC; XPlace itself solves through this exact binary.
        candidates.push_back(h + "/anaconda3/lib/python3.12/site-packages/pulp/solverdir/cbc/linux/i64/cbc");
        candidates.push_back(h + "/miniconda3/lib/python3.12/site-packages/pulp/solverdir/cbc/linux/i64/cbc");
    }
    for (const auto& c : candidates) if (usable(c)) return c;
    return std::string();
}

/**
 * @brief Step 3 — write the LP, solve it with CBC, read the centres back.
 *
 * Model (XPlace macro_legalization_mix):
 *   minimize  sum_i (dx_i + dy_i)                                  total L1 displacement
 *   s.t.      cx_i - ori_x_i <=  dx_i,  cx_i - ori_x_i >= -dx_i    linearised |displacement|
 *             cx_i + (w_i+w_j)/2 <= cx_j                           for each X edge
 *             cy_i + (h_i+h_j)/2 <= cy_j                           for each Y edge
 *             cx_i in [w_i/2, die_w - w_i/2]                       stay in the die
 *
 * @return true if CBC returned an optimal solution and it was applied to @p macros.
 */
bool solveDisplacementLP(std::vector<MacroBox>& macros, const std::vector<EdgeAxis>& edge,
                         double die_w, double die_h, const std::string& cbc,
                         const fs::path& scratch_dir, double* out_displacement)
{
    const int n = (int)macros.size();
    fs::path lp_path  = scratch_dir / "macro_legalize.lp";
    fs::path sol_path = scratch_dir / "macro_legalize.sol";

    {
        std::ofstream lp(lp_path);
        if (!lp) return false;
        lp << "\\ AIEplace macro legalization (minimum total displacement)\nMinimize\n obj:";
        bool first = true;
        for (int i = 0; i < n; i++) {
            if (macros[i].fixed) continue;
            lp << (first ? " " : " + ") << "dx_" << i << " + dy_" << i;
            first = false;
        }
        if (first) return false;   // nothing movable
        lp << "\nSubject To\n";

        for (int i = 0; i < n; i++) {
            if (macros[i].fixed) continue;
            lp << " dxp_" << i << ": x_" << i << " - dx_" << i << " <= " << macros[i].cx << "\n";
            lp << " dxn_" << i << ": x_" << i << " + dx_" << i << " >= " << macros[i].cx << "\n";
            lp << " dyp_" << i << ": y_" << i << " - dy_" << i << " <= " << macros[i].cy << "\n";
            lp << " dyn_" << i << ": y_" << i << " + dy_" << i << " >= " << macros[i].cy << "\n";
        }
        int c = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                EdgeAxis axis = edge[i * n + j];
                if (axis == EdgeAxis::NONE) continue;
                if (macros[i].fixed && macros[j].fixed) continue;   // nothing to solve for
                const char* v = (axis == EdgeAxis::X) ? "x" : "y";
                double sep = edgeWeight(macros[i], macros[j], axis);
                lp << " sep" << c++ << ": " << v << "_" << i << " - " << v << "_" << j
                   << " <= " << -sep << "\n";
            }

        lp << "Bounds\n";
        for (int i = 0; i < n; i++) {
            if (macros[i].fixed) {
                lp << " x_" << i << " = " << macros[i].cx << "\n";
                lp << " y_" << i << " = " << macros[i].cy << "\n";
            } else {
                lp << " " << macros[i].w / 2 << " <= x_" << i << " <= " << die_w - macros[i].w / 2 << "\n";
                lp << " " << macros[i].h / 2 << " <= y_" << i << " <= " << die_h - macros[i].h / 2 << "\n";
                lp << " 0 <= dx_" << i << " <= " << die_w << "\n";
                lp << " 0 <= dy_" << i << " <= " << die_h << "\n";
            }
        }
        lp << "End\n";
    }

    std::string cmd = "\"" + cbc + "\" \"" + lp_path.string() + "\" solve solution \"" +
                      sol_path.string() + "\" > /dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) return false;

    std::ifstream sol(sol_path);
    if (!sol) return false;
    std::string header;
    std::getline(sol, header);
    if (header.find("Optimal") == std::string::npos) return false;

    std::vector<double> nx(n), ny(n);
    for (int i = 0; i < n; i++) { nx[i] = macros[i].cx; ny[i] = macros[i].cy; }
    std::string line;
    while (std::getline(sol, line)) {
        std::istringstream is(line);
        std::string idx, name;
        double value;
        if (!(is >> idx >> name >> value)) continue;
        size_t us = name.find('_');
        if (us == std::string::npos) continue;
        int id = std::atoi(name.c_str() + us + 1);
        if (id < 0 || id >= n) continue;
        if      (name.compare(0, us, "x") == 0) nx[id] = value;
        else if (name.compare(0, us, "y") == 0) ny[id] = value;
    }

    double displacement = 0.0;
    for (int i = 0; i < n; i++) {
        if (macros[i].fixed) continue;
        displacement += std::fabs(nx[i] - macros[i].cx) + std::fabs(ny[i] - macros[i].cy);
        macros[i].cx = nx[i];
        macros[i].cy = ny[i];
    }
    if (out_displacement) *out_displacement = displacement;
    return true;
}

} // namespace

/**
 * @brief Stage 2 proper: legalize the (now frozen) macros in place.
 *
 * Runs after freezeMovableMacros(), so the macros are FIXED components carrying the
 * is_movable_macro tag. Positions are written back through initializeState() so current/next and
 * probe state all agree — a frozen macro is never stepped again, but the density deposit and the
 * DEF writer read those fields.
 */
void Placer::runMacroLegalization()
{
    TIME_FUNCTION();

    std::vector<MacroBox> macros;
    for (Component* comp_p : db.getFixedComponents()) {
        if (!comp_p->isMovableMacro()) continue;   // genuinely-fixed blockages are not ours to move
        MacroBox m;
        m.node_p = comp_p;
        m.w = comp_p->getXsize();
        m.h = comp_p->getYsize();
        m.cx = comp_p->getX() + m.w / 2;
        m.cy = comp_p->getY() + m.h / 2;
        m.fixed = false;
        macros.push_back(m);
    }
    if (macros.size() < 2) {
        Logger::log_detail("Macro legalization: fewer than 2 macros, nothing to do");
        return;
    }

    double worst_before = 0.0;
    int overlaps_before = countOverlaps(macros, &worst_before);
    if (overlaps_before == 0) {
        Logger::log_info("Macro legalization: " + std::to_string(macros.size()) +
                         " macros already non-overlapping, skipped");
        return;
    }

    const double die_w = (double)grid.getDieWidth();
    const double die_h = (double)grid.getDieHeight();

    // Steps 1-2: choose separation directions, then verify each axis' longest path fits the die.
    // A failure here means the direction assignment is infeasible; XPlace repairs it by moving
    // edges between the graphs, we currently report and fall through to the LP, which will then
    // be infeasible and leave the macros untouched. (Refinement is the next thing to port.)
    std::vector<EdgeAxis> edge = buildConstraintGraph(macros);
    std::vector<double> earliest_x, earliest_y;
    bool feasible_x = longestPathEarliest(macros, edge, EdgeAxis::X, die_w, earliest_x);
    bool feasible_y = longestPathEarliest(macros, edge, EdgeAxis::Y, die_h, earliest_y);
    if (!feasible_x || !feasible_y)
        Logger::log_warning(std::string("Macro legalization: constraint graph infeasible on the ") +
                            (!feasible_x ? "x" : "y") + " axis (a chain of macros is wider than "
                            "the die). Longest-path refinement is not ported yet.");

    std::string cbc = findCbc(macro_lp_solver);
    double displacement = 0.0;
    bool solved = false;
    if (!cbc.empty() && feasible_x && feasible_y) {
        solved = solveDisplacementLP(macros, edge, die_w, die_h, cbc, output_dir, &displacement);
        if (!solved) Logger::log_warning("Macro legalization: CBC did not return an optimal "
                                         "solution; falling back to the longest-path placement");
    } else if (cbc.empty()) {
        Logger::log_warning("Macro legalization: no CBC binary found (set params.macro_lp_solver); "
                            "falling back to the longest-path placement");
    }

    if (!solved && feasible_x && feasible_y) {
        // Fallback: the longest-path earliest positions satisfy every constraint by construction,
        // so they are legal — just not minimum-displacement.
        for (size_t i = 0; i < macros.size(); i++) {
            displacement += std::fabs(earliest_x[i] - macros[i].cx) +
                            std::fabs(earliest_y[i] - macros[i].cy);
            macros[i].cx = earliest_x[i];
            macros[i].cy = earliest_y[i];
        }
        solved = true;
    }
    if (!solved) {
        Logger::log_error("Macro legalization FAILED; macros left overlapping (" +
                          std::to_string(overlaps_before) + " pairs)");
        return;
    }

    double worst_after = 0.0;
    int overlaps_after = countOverlaps(macros, &worst_after);
    for (const MacroBox& m : macros)
        m.node_p->initializeState(Position((float)(m.cx - m.w / 2), (float)(m.cy - m.h / 2)));

    Logger::log_info("Macro legalization: " + std::to_string(macros.size()) + " macros, overlap "
                     "pairs " + std::to_string(overlaps_before) + " -> " +
                     std::to_string(overlaps_after) + ", total displacement " +
                     PREC(displacement) + (solved && !cbc.empty() ? "  [LP/CBC]" : "  [longest-path]"));
    if (overlaps_after > 0)
        Logger::log_warning("Macro legalization: " + std::to_string(overlaps_after) +
                            " overlapping pairs remain (worst " + SCI(worst_after) +
                            " area). Needs the longest-path refinement pass.");
}

AIEPLACE_NAMESPACE_END

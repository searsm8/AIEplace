/**
 * @file MacroLegalize.cpp
 * @brief LP-based macro legalization — stage 2 of the mixed-size flow (TODO #13).
 *
 * Port of XPlace's `src/core/macro_legalization.py`. A full walkthrough of the algorithm lives in
 * `.claude/1_REVIEW/NEW_EXPLAINER_lp_macro_legalization.md`; the short version:
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
#include <numeric>
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
 * position. Mirrors XPlace's implicit source node (a virtual predecessor with edge weight =
 * half the macro's own size) by seeding every node at its own half-extent before relaxing.
 * Returns false if the graph has a cycle (should not happen: edges follow position order at
 * construction, and every repair move is chosen to preserve the DAG property).
 */
bool computeEarliest(const std::vector<MacroBox>& macros, const std::vector<EdgeAxis>& edge,
                     EdgeAxis axis, std::vector<double>& L)
{
    const int n = (int)macros.size();
    L.assign(n, 0.0);
    for (int i = 0; i < n; i++) {
        const MacroBox& m = macros[i];
        double half = (axis == EdgeAxis::X) ? m.w / 2 : m.h / 2;
        L[i] = m.fixed ? ((axis == EdgeAxis::X) ? m.cx : m.cy) : half;
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
            double need = L[u] + edgeWeight(macros[u], macros[v], axis);
            if (!macros[v].fixed) L[v] = std::max(L[v], need);
            if (--indeg[v] == 0) order.push_back(v);
        }
    }
    return (int)order.size() == n;   // false = cycle
}

/**
 * @brief Symmetric sweep from the high-die-edge "sink": the latest feasible centre for every
 *        macro (STA required-time / ALAP). Reverse Kahn order: a node resolves once every
 *        successor on this axis already has its latest value, mirroring XPlace's implicit sink
 *        node (weight = half the macro's own size) via the die_extent-half seed.
 */
bool computeLatest(const std::vector<MacroBox>& macros, const std::vector<EdgeAxis>& edge,
                   EdgeAxis axis, double die_extent, std::vector<double>& R)
{
    const int n = (int)macros.size();
    R.assign(n, 0.0);
    for (int i = 0; i < n; i++) {
        const MacroBox& m = macros[i];
        double half = (axis == EdgeAxis::X) ? m.w / 2 : m.h / 2;
        R[i] = m.fixed ? ((axis == EdgeAxis::X) ? m.cx : m.cy) : (die_extent - half);
    }

    std::vector<int> outdeg(n, 0);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (edge[i * n + j] == axis) outdeg[i]++;

    std::vector<int> order;
    order.reserve(n);
    for (int i = 0; i < n; i++) if (outdeg[i] == 0) order.push_back(i);
    for (size_t k = 0; k < order.size(); k++) {
        int v = order[k];
        for (int u = 0; u < n; u++) {
            if (edge[u * n + v] != axis) continue;
            double allowed = R[v] - edgeWeight(macros[u], macros[v], axis);
            if (!macros[u].fixed) R[u] = std::min(R[u], allowed);
            if (--outdeg[u] == 0) order.push_back(u);
        }
    }
    return (int)order.size() == n;   // false = cycle
}

/// L and R for both axes, and the node-slack helpers built on top of them (XPlace's L/R/slack_v).
struct LRState {
    std::vector<double> Lx, Rx, Ly, Ry;
};

/// Full recompute of L and R on both axes over the current @p edge graph. sw_only's macro counts
/// (tens to low hundreds) make XPlace's incremental affected-node BFS unnecessary — a full O(n +
/// E) sweep per call is already cheap, and there are at most 5 trials x n macros of these.
bool computeLR(const std::vector<MacroBox>& macros, const std::vector<EdgeAxis>& edge,
               double die_w, double die_h, LRState& s)
{
    bool ok = true;
    ok &= computeEarliest(macros, edge, EdgeAxis::X, s.Lx);
    ok &= computeLatest(macros, edge, EdgeAxis::X, die_w, s.Rx);
    ok &= computeEarliest(macros, edge, EdgeAxis::Y, s.Ly);
    ok &= computeLatest(macros, edge, EdgeAxis::Y, die_h, s.Ry);
    return ok;
}

/// XPlace's TNS (total negative slack): sum over every node and axis of min(slack, 0). Zero (up
/// to floating tolerance) means every node's [L, R] window is non-empty on both axes — a
/// feasible direction assignment provably exists.
double totalNegativeSlack(const LRState& s)
{
    double total = 0.0;
    for (size_t i = 0; i < s.Lx.size(); i++) {
        total += std::min(0.0, s.Rx[i] - s.Lx[i]);
        total += std::min(0.0, s.Ry[i] - s.Ly[i]);
    }
    return total;
}

/// One constraint-graph edge migration: delete (u_del -> v_del) from G_{axis_del}, add
/// (u_add -> v_add) to G_{axis_add}.
struct EdgeMove {
    EdgeAxis axis_del; int u_del, v_del;
    EdgeAxis axis_add; int u_add, v_add;
};

/**
 * @brief Port of XPlace's mark_edge_to_move (macro_legalization.py:287). For a macro @p i whose
 *        slack is negative on exactly one axis, find every predecessor j whose edge into i is
 *        BINDING on that axis (L[i] == L[j] + weight(j,i)) and check whether re-routing j->i
 *        onto the other axis is feasible without breaking any of j's (or i's) existing edges on
 *        that axis. This is XPlace's "quick estimate" — it checks one hop of fanout rather than
 *        re-running full propagation, which is why the caller re-derives L/R from scratch after
 *        applying whatever this returns instead of trusting the estimate as final.
 *
 *        Ported literally, including one XPlace quirk: inside the j->i (edge_ji) candidate's
 *        fanout check, a failure clears `edge_ij` rather than `edge_ji` (macro_legalization.py
 *        :330-333 vs :312-320 has the same pattern but the assignment targets don't mirror it).
 *        Left as-is rather than silently correcting the reference algorithm — the outer trial
 *        loop always re-verifies feasibility from a full L/R recompute, so a wrongly-accepted
 *        candidate here is caught, not trusted.
 */
std::vector<EdgeMove> markEdgeToMove(const std::vector<MacroBox>& macros,
                                     const std::vector<EdgeAxis>& edge, int i, const LRState& s)
{
    const int n = (int)macros.size();
    std::vector<EdgeMove> moves;
    const double eps = 1e-9;

    double x_slack = s.Rx[i] - s.Lx[i];
    double y_slack = s.Ry[i] - s.Ly[i];
    if ((x_slack >= 0 && y_slack >= 0) || (x_slack < 0 && y_slack < 0)) return moves;
    EdgeAxis axis   = (x_slack >= 0 && y_slack < 0) ? EdgeAxis::Y : EdgeAxis::X;
    EdgeAxis o_axis = (axis == EdgeAxis::X) ? EdgeAxis::Y : EdgeAxis::X;
    const std::vector<double>& L_axis = (axis == EdgeAxis::X) ? s.Lx : s.Ly;
    const std::vector<double>& L_o    = (axis == EdgeAxis::X) ? s.Ly : s.Lx;
    const std::vector<double>& R_o    = (axis == EdgeAxis::X) ? s.Ry : s.Rx;

    for (int j = 0; j < n; j++) {
        if (i == j || edge[j * n + i] != axis) continue;               // j -> i on axis?
        double w_ji = edgeWeight(macros[j], macros[i], axis);
        if (std::fabs(L_axis[i] - (L_axis[j] + w_ji)) > eps) continue;  // not the binding pred

        bool edge_ij = false, edge_ji = false;
        double w_ij_o = edgeWeight(macros[i], macros[j], o_axis);

        // Candidate: i -> j on o_axis. Check j's existing o_axis successors k stay feasible.
        if (L_o[i] + w_ij_o <= R_o[j] + eps) {
            edge_ij = true;
            for (int k = 0; k < n && edge_ij; k++) {
                if (edge[j * n + k] != o_axis || k == j) continue;
                double w_jk = edgeWeight(macros[j], macros[k], o_axis);
                if (L_o[i] + w_ij_o + w_jk > R_o[k] + eps) edge_ij = false;
                else if (L_o[j] + w_jk > R_o[k] + eps) edge_ij = false;
            }
        }
        // Candidate: j -> i on o_axis. Check i's existing o_axis successors k stay feasible.
        if (L_o[j] + w_ij_o <= R_o[i] + eps) {
            edge_ji = true;
            for (int k = 0; k < n && edge_ji; k++) {
                if (edge[i * n + k] != o_axis || k == i) continue;
                double w_ik = edgeWeight(macros[i], macros[k], o_axis);
                // XPlace quirk (see docstring): both failure branches clear edge_ij here.
                if (L_o[j] + w_ij_o + w_ik > R_o[k] + eps) edge_ij = false;
                else if (L_o[i] + w_ik > R_o[k] + eps) edge_ij = false;
            }
        }

        if (edge_ij && edge_ji) {
            // Both directions feasible: orient by current position order (keeps displacement
            // small), same convention as buildConstraintGraph.
            bool i_first = (o_axis == EdgeAxis::X) ? (macros[i].cx <= macros[j].cx)
                                                    : (macros[i].cy <= macros[j].cy);
            moves.push_back({axis, j, i, o_axis, i_first ? i : j, i_first ? j : i});
        } else if (edge_ij) {
            moves.push_back({axis, j, i, o_axis, i, j});
        } else if (edge_ji) {
            moves.push_back({axis, j, i, o_axis, j, i});
        } else {
            moves.clear();   // XPlace: a single infeasible candidate voids this macro's round
        }
    }
    return moves;
}

/**
 * @brief Step 2 — repair the constraint graph so every node's [L, R] window is non-empty on
 *        both axes. Port of XPlace's longest_path_refinement repair loop
 *        (macro_legalization.py:353): while total negative slack is negative and fewer than 5
 *        trials have run, visit macros smallest-area-first (ties broken by slack, then position,
 *        then index — XPlace's exact tiebreak) and migrate any binding edge mark_edge_to_move
 *        finds onto the axis with room. Mutates @p edge and @p state in place.
 */
void runLongestPathRefinement(const std::vector<MacroBox>& macros, std::vector<EdgeAxis>& edge,
                              double die_w, double die_h, LRState& state)
{
    const int n = (int)macros.size();
    int trials = 0;
    while (totalNegativeSlack(state) < -1e-9 && trials < 5) {
        trials++;
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            double area_a = macros[a].w * macros[a].h, area_b = macros[b].w * macros[b].h;
            if (area_a != area_b) return area_a < area_b;               // smallest area first
            double slack_a = (state.Rx[a] - state.Lx[a]) + (state.Ry[a] - state.Ly[a]);
            double slack_b = (state.Rx[b] - state.Lx[b]) + (state.Ry[b] - state.Ly[b]);
            if (slack_a != slack_b) return slack_a > slack_b;           // XPlace sorts by -sum_slack
                                                                          // ascending == largest slack first
            if (macros[a].cx != macros[b].cx) return macros[a].cx < macros[b].cx;
            if (macros[a].cy != macros[b].cy) return macros[a].cy < macros[b].cy;
            return a < b;
        });

        for (int i : order) {
            std::vector<EdgeMove> moves = markEdgeToMove(macros, edge, i, state);
            if (moves.empty()) continue;
            for (const EdgeMove& mv : moves) {
                edge[mv.u_del * n + mv.v_del] = EdgeAxis::NONE;
                edge[mv.u_add * n + mv.v_add] = mv.axis_add;
            }
            if (!computeLR(macros, edge, die_w, die_h, state)) {
                Logger::log_warning("Macro legalization: longest-path refinement produced a "
                                    "cycle; stopping repair with the best graph found so far");
                return;
            }
        }
    }
    if (totalNegativeSlack(state) < -1e-9)
        Logger::log_warning("Macro legalization: longest-path refinement did not reach feasibility "
                            "after 5 trials (total negative slack " + PREC(totalNegativeSlack(state)) +
                            "); a chain of macros is genuinely wider than the die on some axis");
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

    // Step 1: choose separation directions. Step 2: repair the guess (longest-path refinement,
    // XPlace macro_legalization.py:353) -- migrate edges between G_x/G_y until every macro's
    // [L, R] window is non-empty on both axes, i.e. a feasible direction assignment exists.
    std::vector<EdgeAxis> edge = buildConstraintGraph(macros);
    LRState state;
    computeLR(macros, edge, die_w, die_h, state);
    double slack_before = totalNegativeSlack(state);
    if (slack_before < -1e-9) runLongestPathRefinement(macros, edge, die_w, die_h, state);
    bool feasible = totalNegativeSlack(state) > -1e-9;
    if (slack_before < -1e-9)
        Logger::log_detail("Macro legalization: longest-path refinement total negative slack " +
                           PREC(slack_before) + " -> " + PREC(totalNegativeSlack(state)) +
                           (feasible ? "  (repaired)" : "  (still infeasible)"));

    std::string cbc = findCbc(macro_lp_solver);
    double displacement = 0.0;
    bool solved = false;
    if (!cbc.empty()) {
        solved = solveDisplacementLP(macros, edge, die_w, die_h, cbc, output_dir, &displacement);
        if (!solved) Logger::log_warning("Macro legalization: CBC did not return an optimal "
                                         "solution; falling back to the longest-path placement");
    } else {
        Logger::log_warning("Macro legalization: no CBC binary found (set params.macro_lp_solver); "
                            "falling back to the longest-path placement");
    }

    if (!solved && feasible) {
        // Fallback: the (now-repaired) longest-path earliest positions satisfy every constraint
        // by construction, so they are legal — just not minimum-displacement.
        for (size_t i = 0; i < macros.size(); i++) {
            displacement += std::fabs(state.Lx[i] - macros[i].cx) +
                            std::fabs(state.Ly[i] - macros[i].cy);
            macros[i].cx = state.Lx[i];
            macros[i].cy = state.Ly[i];
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
                            " area) even after longest-path refinement" +
                            (feasible ? "" : " (still infeasible after 5 repair trials)") +
                            ". Not yet ported: macro_legalization_xy variant, site/row alignment, "
                            "retry-with-longer-CBC-time-limit.");
}

AIEPLACE_NAMESPACE_END

/**
 * @file BestSolution.cpp
 * @brief The best-solution machinery: the three trackers (primary / aux / rollback), the rule
 *        that picks which one to ship, and the geometry snapshot/restore that carries it.
 *
 * Mirrors XPlace's ParamScheduler best-solution handling (param_scheduler.py:94-101, 540-577).
 * Gathered here from AIEplace.cpp (the geometry buffers), Schedule.cpp (the selection rule) and
 * Output.cpp (the final restore) so the one concept lives in one file (2026-08-31 cleanup).
 */

#include "AIEplace.h"
#include <string>

AIEPLACE_NAMESPACE_BEGIN

/// @brief The snapshot buffer belonging to one tracker. Selecting by slot rather than sharing one
///        buffer is what makes the shipped geometry match the solution the rule picked (TODO #24).
static inline Position& bestSlotPos(Node* node_p, Placer::BestSlot slot)
{
    switch (slot) {
        case Placer::BestSlot::AUX:      return node_p->best_aux_pos;
        case Placer::BestSlot::ROLLBACK: return node_p->best_rollback_pos;
        default:                         return node_p->best_primary_pos;
    }
}

/// @brief Save current movable + filler positions into one tracker's snapshot.
///
/// Snapshots the LOOKAHEAD v_k, not the committed u (TODO #32/7a). XPlace has only one position
/// variable -- `p` IS `v_k` (nesterov_optimizer.py:71, "directly use p as v_k to save memory") --
/// so `ps.step(hpwl, overflow, mov_node_pos, ...)` stores v_k and `evaluator_fn(mov_node_pos)`
/// measures BOTH metrics there (run_placement_nesterov.py:142-145). One self-consistent placement.
/// We used to store u while measuring HPWL at u and overflow at v, which is a pair no single
/// iteration ever held; recordIterationResults() now measures HPWL at v to match.
void Placer::snapshotBestPlacement(BestSlot slot)
{
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++)
        bestSlotPos(nodes[i], slot) = nodes[i]->next.probe_pos;
}

/// @brief Restore movable + filler positions from one tracker's snapshot -- BOTH halves of the pair.
///
/// A placement here is the pair (node_pos, probe_pos): HPWL reads node_pos (Net.h:25) while every
/// density/overflow metric deposits at probe_pos (computeNodeFootprint, Grid.cpp:36). Writing only
/// node_pos would leave the node in a state that existed at no point in the run -- snapshot
/// position from one iteration, lookahead from another -- so the reported overflow would describe
/// the last iteration rather than the placement being shipped (TODO #24).
///
/// Since TODO #32/7a the snapshot holds v_k, so restoring it into both fields reconstructs
/// XPlace's single position variable exactly (nesterov_optimizer.py:71): after this call u == v,
/// which is the state XPlace is always in. This subsumes the old separate syncProbeToCommitted().
void Placer::restoreBestPlacement(BestSlot slot)
{
    // CLAUDE CODE: the nodes are about to jump to a placement the last density solve and the last
    // gradient evaluation never saw, so both auxiliary dump channels stop describing them. The
    // final "best_solution" frame is exactly this case: without clearing here it would carry the
    // last ITERATED placement's heatmap under the RESTORED placement's cells.
    pos_dump.density_fresh = false;
    pos_dump.forces_fresh  = false;

    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++) {
        const Position& saved = bestSlotPos(nodes[i], slot);
        nodes[i]->next.node_pos  = saved;
        nodes[i]->next.probe_pos = saved;
    }
}

/// @brief The divergence guards' reference metric — deliberately NOT the shipping rule. This one
///        feeds stopping criteria, so its population must stay as wide as it was before TODO #24
///        (any tracker, converged or not); selectBestSolution() answers the different question of
///        which placement to hand over at the end.
const Placer::BestSolution& Placer::bestReference() const
{
    return best_primary.valid  ? best_primary
         : best_aux.valid      ? best_aux
         : best_rollback.valid ? best_rollback
         : best_primary;
}

/**
 * @brief Which solution to ship. Ported from XPlace get_best_solution (param_scheduler.py:540-577):
 *        a surviving rollback wins outright (it only survives when the run never converged, so
 *        nothing else exists); otherwise PREFER the lower-overflow aux, but only when it costs
 *        <= 0.5% HPWL and buys >= 10% overflow. The default lean is toward the spread-out solution.
 */
Placer::BestChoice Placer::selectBestSolution() const
{
    if (best_rollback.valid)
        return {&best_rollback, BestSlot::ROLLBACK, "rollback (never converged)"};
    if (!best_primary.valid && !best_aux.valid) return {};
    if (!best_aux.valid)     return {&best_primary, BestSlot::PRIMARY, "primary (HPWL driven)"};
    if (!best_primary.valid) return {&best_aux,     BestSlot::AUX,     "aux (overflow driven)"};

    const bool aux_worth_its_hpwl = (best_aux.hpwl < best_primary.hpwl * aux_select_hpwl_ratio &&
                                     best_aux.overflow * AUX_SELECT_OVFW_RATIO < best_primary.overflow);
    return aux_worth_its_hpwl ? BestChoice{&best_aux,     BestSlot::AUX,     "aux (overflow driven)"}
                              : BestChoice{&best_primary, BestSlot::PRIMARY, "primary (HPWL driven)"};
}

/// @brief Apply selectBestSolution() and restore THAT tracker's geometry. The slot comes from the
///        same struct as the metadata being logged, so the two cannot disagree (TODO #24).
Placer::BestChoice Placer::restoreBestSolution()
{
    BestChoice chosen = selectBestSolution();

    if (chosen.sol) {
        restoreBestPlacement(chosen.slot);   // restores both halves; everything below reports on it
        Logger::log_info("Restored " + std::string(chosen.type) + " best placement from iteration " +
            std::to_string(chosen.sol->iteration) +
            " (HPWL: " + std::to_string(chosen.sol->hpwl) +
            ", overflow: " + std::to_string(chosen.sol->overflow) + ")");
    } else {
        Logger::log_info("No best placement saved (solver may not have stabilized). Using last solution.");
    }
    return chosen;
}

AIEPLACE_NAMESPACE_END

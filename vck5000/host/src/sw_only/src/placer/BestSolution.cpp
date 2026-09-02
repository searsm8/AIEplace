/**
 * @file BestSolution.cpp
 * @brief The best-solution machinery: recording each iteration into the three trackers
 *        (primary / aux / rollback), the rule that picks which one to ship, and the geometry
 *        snapshot/restore that carries it.
 *
 * Mirrors XPlace's ParamScheduler best-solution handling (param_scheduler.py:94-101, 390-451,
 * 540-577). Gathered here from AIEplace.cpp (the geometry buffers), Schedule.cpp (the selection
 * rule) and Output.cpp (the recorder and the final restore) so the one concept lives in one file
 * (2026-08-31 cleanup; recordIterationResults() joined it 2026-09-02).
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

/**
 * @brief Record this iteration's HPWL/overflow, then update whichever of the three trackers
 *        (rollback/aux/primary) it qualifies for. Ported from XPlace's update_best_sol
 *        (param_scheduler.py:390-451). Called once per iteration from performIteration().
 */
void Placer::recordIterationResults()
{
    TIME_FUNCTION();
    // Measured at the LOOKAHEAD v_k (at_probe = true), matching XPlace's single position variable
    // (TODO #32/7a; see snapshotBestPlacement). This is the one `hpwl` XPlace's evaluator_fn
    // produces: it feeds the recorder's delta_hpwl, convergence, AND update_best_sol alike, so all
    // three describe the same placement as the overflow computed just below.
    float hpwl = db.computeTotalWirelength(ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"), cfg["params"]["ignore_net_degree"].value_or(100), true);
    // Drive convergence off the smoothed overflow (clamped footprints; equivalent to XPlace's
    // expand_ratio-inflated field): the smoothed density the optimizer minimizes, which descends
    // toward the stop threshold. The exact overflow is reported separately as the physical result.
    // Fillers are EXCLUDED, as XPlace's overflow_fn excludes them: it runs on `mov_density_map`,
    // the movable-only slice `[mov_lhs:mov_rhs]`, and `filler_density_map` (`[mov_rhs:]`) is added
    // only afterwards, and only for the FORCE (electronic_density_layer.py:36-50, 272-292;
    // fillers are appended past mov_rhs by get_mov_node_info, database.py:901-904). The reported
    // "exact Overflow" excludes them too (get_obj_overflow, evaluator.py:26-50).
    // A `convergence_include_fillers` toggle briefly forced this TRUE in phase 2 (2026-08-02) on
    // the opposite belief; retracted and deleted 2026-08-07 after the 16-design A/B — TODO #19a.
    float overflow = computeOverflow(true, nullptr, false); // convergence signal

    hpwl_history.push_back(hpwl);
    step_length_history.push_back(step_length);
    ovfw_history.push_back(overflow);

    // Best-solution tracking, ported from XPlace's update_best_sol (param_scheduler.py:390-451).
    // Skip early iterations to let the solver stabilize (XPlace: `iter - init_iter < 50`).
    // PHASE-RELATIVE, as XPlace's is (param_scheduler.py:393, and init_iter is reset at every
    // optimizer restart): after the phase-2 mixed-size restart the solver is re-seeded and
    // re-estimates its step, so it needs the same 50-iteration settling window phase 1 got.
    // This was absolute until TODO #32/7b, which made phase 2 start tracking immediately.
    if (phaseIteration() < BEST_SOL_MIN_ITER) return;

    const bool converged_now = (overflow < overflow_threshold);

    // XPlace frees the rollback net on the FIRST converged iteration (param_scheduler.py:396-405).
    // That lifetime is what lets the selection rule give rollback absolute priority: if it is still
    // around, the run never converged and there is nothing else to choose. Once dropped it stays
    // dropped, even if overflow later climbs back above the threshold.
    if (converged_now && !ever_converged) {
        ever_converged = true;
        best_rollback  = BestSolution{};
    }

    // Rollback: near-converged band, before any converged solution exists. Overflow must improve;
    // HPWL is allowed to creep 1% to buy it (param_scheduler.py:407-428).
    if (!ever_converged && overflow < 5.0f * overflow_threshold &&
        hpwl < best_rollback.hpwl * ROLLBACK_UPDATE_HPWL_RATIO && overflow < best_rollback.overflow)
    {
        best_rollback = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::ROLLBACK);
    }

    // Aux: converged, driving overflow down, paying at most 0.5% HPWL per update (:434-441).
    // This is NOT a divergence guard -- it is the spread-out solution the selection rule PREFERS.
    // Measured against the aux snapshot's OWN previous HPWL, which is what makes this a different
    // budget from aux_select_hpwl_ratio despite both being 0.5% (TODO #33).
    if (converged_now && hpwl < best_aux.hpwl * AUX_UPDATE_HPWL_RATIO && overflow < best_aux.overflow) {
        best_aux = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::AUX);
    }

    // Primary: converged, lowest HPWL (:444-450).
    if (converged_now && hpwl < best_primary.hpwl) {
        best_primary = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::PRIMARY);
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

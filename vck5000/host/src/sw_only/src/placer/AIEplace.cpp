#include "DCT.h"
#include "AIEplace.h"
#include <cassert>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Run the ePlace algorithm
 * Initialize the placement, then perform iterations until the convergence condition is met.
 */
void Placer::run()
{
    initializePlacement();
    performIterationZero();

    while( true )
    {
        performIteration();
        if (checkConvergence()) {
            // On a mixed-size design the phase-1 stop is not the end of the run: it is XPlace's
            // handoff to macro legalization plus a second standard-cell pass with the macros
            // frozen (TODO #13). beginFixedMacroPhase() returns false whenever that handoff does
            // not apply, which is what keeps every single-phase design bit-identical.
            if (beginFixedMacroPhase())
                continue;
            break;
        }
        if (m_nan_detected) {
            m_stop_reason = StopReason::NAN_PARTIALS;
            break;
        }
    }
}

/**
 * @brief Run one placement iteration: compute the wirelength and density gradients, combine
 *        them, take a Barzilai-Borwein (optionally backtracked) Nesterov step, then update the
 *        gamma/lambda schedule and best-solution tracking.
 */
void Placer::performIteration()
{
    TIME_FUNCTION();
    ++iteration;
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));

    updatePrecondWeights();

    // Phase-relative: phase 2 re-estimates its own initial step after the restart, exactly as
    // phase 1 did (XPlace re-runs estimate_initial_learning_rate at the optimizer reset).
    if (phaseIteration() == 1)
        estimateInitialStep();

    performNextStep(enable_backtracking);

    recordIterationResults();
    printIterationResults();

    updateSchedule();

    if (m_nan_detected)
        Logger::log_error("Stopping: NaN in HPWL partials at iteration " +
                          std::to_string(iteration) + " (hard divergence)");
}

/**
 * @brief Iteration-zero bootstrap: compute the first gradients and initialize solver state,
 *        before the first numbered iteration runs. Probe positions (v_1 = u_1) are already
 *        set by initializePlacement().
 */
void Placer::performIterationZero()
{
    iterationReset();

    computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    computeElectricFields();    // ∇D from ρ → bin eFields

    initializeDensityWeight();
}

Placer::Placer(std::string config_filepath)
{
    m_config_filepath = config_filepath;

    setupDesign();
    Logger::log_detail("Database setup time: " + 
            std::to_string(Logger::getFunctionTime("setupDesign") / 1.0e6) + " s");

    setupGrid();
    createRunOutputStructure();
    configureGammaSchedule();
    initializePositionDump();
}

/**
 * @brief Reset all nodes and nets in preparation for the next iteration
 */
void Placer::iterationReset()
{
    TIME_FUNCTION();
    grid.iterationReset();
    db.iterationReset();
}


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
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++) {
        const Position& saved = bestSlotPos(nodes[i], slot);
        nodes[i]->next.node_pos  = saved;
        nodes[i]->next.probe_pos = saved;
    }
}

AIEPLACE_NAMESPACE_END
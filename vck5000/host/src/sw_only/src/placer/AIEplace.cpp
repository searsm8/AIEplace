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
    recordInitialHPWL();
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
    initializeVisualization();
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


/// @brief Save current movable + filler positions as the best-so-far solution (divergence guard).
void Placer::snapshotBestPlacement()
{
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++)
        nodes[i]->best_solution_pos = nodes[i]->next.node_pos;
}

/// @brief Restore movable + filler positions from the saved best-so-far solution.
void Placer::restoreBestPlacement()
{
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++)
        nodes[i]->next.node_pos = nodes[i]->best_solution_pos;
}

AIEPLACE_NAMESPACE_END
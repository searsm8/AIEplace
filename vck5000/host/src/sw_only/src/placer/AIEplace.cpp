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
        if(checkConvergence() || m_nan_detected )
            break;
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

    if (iteration == 1)
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
    Logger::log_info("Database setup time: " + 
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
    grid.iterationReset();
    db.iterationReset();
}


/// @brief Save current movable + filler positions as the best-so-far solution (divergence guard).
void Placer::snapshotBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->best_solution_pos = item.second->next.node_pos;
    }
    for (auto filler_p : db.getFillers())
        filler_p->best_solution_pos = filler_p->next.node_pos;
}

/// @brief Restore movable + filler positions from the saved best-so-far solution.
void Placer::restoreBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.node_pos = item.second->best_solution_pos;
    }
    for (auto filler_p : db.getFillers())
        filler_p->next.node_pos = filler_p->best_solution_pos;
}

AIEPLACE_NAMESPACE_END
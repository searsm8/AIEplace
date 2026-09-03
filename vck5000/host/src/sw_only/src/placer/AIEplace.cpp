/**
 * @file AIEplace.cpp
 * @brief The algorithm's top level loop, orchestrating the whole algorithm.
 *        Follow the calls out into Setup/Step/Partials/Density/Schedule.cpp for functionality.
 */
#include "DCT.h"
#include "AIEplace.h"
#include <cassert>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Run the ePlace algorithm.
 *        Perform iterations until the convergence condition is met.
 */
void Placer::run()
{
    initializePlacement();
    performIterationZero();

    while( true )
    {
        performIteration();

        if (checkConvergence()) {
            // MMS benchmarks need Phase 2 with macros locked
            if (phase == Phase::MIXED_SIZE && readyForPhase2()) {
                beginPhase2();
            } 
            else break;
        }
        if (checkForNaN()) break; // NaN detected, very bad, stop immediately
    }
}

/**
 * @brief Run one placement iteration: compute the wirelength and density gradients, combine them,
 *        take a Nesterov step, then update thegamma/lambda schedule and best-solution tracking.
 */
void Placer::performIteration()
{
    TIME_FUNCTION();
    ++iteration;
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));

    updatePrecondWeights();

    if (phaseIteration() == 1)
        estimateInitialStep();

    performNextStep();
    recordIterationResults();
    printIterationResults();

    updateSchedule(); // Update γ, λ, and preconditioner for the next iteration. 

    dumpScheduleTrace();
}

/**
 * @brief Iteration-zero bootstrap: compute the first gradients and initialize solver state
 *        Two callers: run() and beginPhase2().
 */
void Placer::performIterationZero()
{
    iterationReset();

    computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    computeElectricFields();    // ∇D from ρ → bin eFields

    initializeDensityWeight();
}

/**
 * @brief Constructor: read the config file, parse the design and grid, and initialize the schedule.
 */
Placer::Placer(std::string config_filepath_arg)
{
    config_filepath = config_filepath_arg;

    setupDesign();
    createRunOutputStructure();
    configureGammaSchedule();
    initializePositionDump();
}

AIEPLACE_NAMESPACE_END

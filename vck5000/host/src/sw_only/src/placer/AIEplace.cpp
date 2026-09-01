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
            // MMS benchmarks run with Phase::MIXED_SIZE.
            // Upon converging, lock macros and begin phase 2 (Phase::STDCELL_FIXED_MACRO).
            if (phase == Phase::MIXED_SIZE && readyForPhase2()) {
                beginPhase2();
                continue;
            }
            break;
        }

        if (nan_detected) {
            stop_reason = StopReason::NAN_PARTIALS;
            break;
        }
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

    performNextStep(enable_backtracking);
    recordIterationResults();
    printIterationResults();

    updateSchedule();

    if (cfg["output"]["dump_schedule_trace"].value_or(false))
        dumpScheduleTrace();

    if (nan_detected)
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

Placer::Placer(std::string config_filepath_arg)
{
    config_filepath = config_filepath_arg;

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

AIEPLACE_NAMESPACE_END

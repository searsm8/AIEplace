/**
 * @file Phase2.cpp
 * @brief The mixed-size phase-1 -> phase-2 transition (TODO #13).
 *
 * XPlace's mixed-size flow is three stages (run_placement_nesterov.py:167-230):
 *   1. Mixed-GP        — macros and standard cells placed together. This is phase 1.
 *   2. Macro legalization — an LP that removes macro overlap at minimum displacement.
 *   3. A second full GP pass with the macros FIXED, standard cells re-seeded from scratch.
 *
 * The thing that makes stage 3 work is not subtle: while a macro is movable it is an
 * incompressible lump of area that the density field can never satisfy at target_density < 1,
 * so it radiates a standing force forever. Freezing it turns it into an obstacle the standard
 * cells simply flow around. XPlace's own newblue5 goes 0.1697 -> 0.0452 exact overflow across
 * this boundary.
 *
 * Phase 1's only durable output is therefore the MACRO positions — the standard-cell placement
 * it worked so hard on is discarded (XPlace re-randomises with init_method="randn_center").
 */

#include "AIEplace.h"
#include <cmath>

AIEPLACE_NAMESPACE_BEGIN

const char* Placer::phaseName(Phase phase)
{
    switch (phase) {
        case Phase::MIXED_SIZE:          return "mixed_size";
        case Phase::STDCELL_FIXED_MACRO: return "stdcell_fixed_macro";
    }
    return "unknown";
}

/**
 * @brief Take the phase-1 -> phase-2 transition, if this run warrants one.
 *
 * Called from run() when checkConvergence() says phase 1 is over. Returns false to let the run
 * end exactly as it did before phase 2 existed, which is what keeps every non-mixed-size design
 * bit-identical.
 */
bool Placer::beginFixedMacroPhase()
{
    if (!enable_phase2)               return false;   // config opt-out
    if (m_phase != Phase::MIXED_SIZE) return false;   // already done it
    if (!mixed_size_mode)             return false;   // no movable macros: nothing to freeze

    // Which phase-1 endings earn a phase 2 (Mark's call, 2026-08-01):
    //   converged / divergence_guard -> YES. In XPlace the early-stop signal IS the handoff
    //     trigger: need_to_early_stop() sets terminate_signal, which runs macro legalization
    //     and the optimizer reset, then clears the signal and continues.
    //   diverged_hpwl / NaN          -> NO. A genuinely diverged phase 1 has no macro placement
    //     worth freezing, and restarting from one would launder a bad result into a
    //     respectable-looking phase-2 number.
    //   max_iterations               -> NO. Phase 1 never finished; its macros are not settled,
    //     and the absolute iteration budget is already spent.
    const bool eligible = (m_stop_reason == StopReason::CONVERGED ||
                           m_stop_reason == StopReason::DIVERGENCE_GUARD);
    if (!eligible) {
        Logger::log_info("Phase 2 skipped: phase 1 ended with reason=" +
                         std::string(stopReasonName(m_stop_reason)) +
                         " (macro positions are not trustworthy)");
        return false;
    }

    reportPhaseSummary();   // record + print phase 1 BEFORE anything is mutated

    // Phase 1's deliverable is where the macros ended up, and best-solution tracking usually holds
    // a better placement than the last iteration's. Guard the restore: best_solution_pos is only
    // meaningful once snapshotBestPlacement() has run, and a run that stopped before any best was
    // recorded would otherwise be "restored" to uninitialised positions.
    if (bestReference().valid) restoreBestPlacement();
    else Logger::log_warning("Phase 2: no best placement recorded in phase 1; "
                             "freezing the macros at their last iterated positions");

    // Freeze BEFORE legalizing. The order matters: freezeMovableMacros() is what moves the macros
    // into getFixedComponents() and collapses their state onto the committed position, and the
    // legalizer identifies its work set as "fixed components still carrying the is_movable_macro
    // tag". Legalizing first would hand it an empty set.
    int frozen = db.freezeMovableMacros();
    legalizeMacros();

    // Frame the legalization on its own, before the re-seed wipes the phase-1 cell placement:
    // this is the only picture in the run where the LP's macro displacement is visible.
    // TODO #16: freezeMovableMacros() moved the macros out of the movable list, so the node set
    // this frame describes is no longer generation 0's. Close it and start a new one before
    // recording the legalized placement.
    beginPositionDumpGeneration();
    dumpIterationPositions("legalized");

    target_density = db.rebuildFillers(target_density);
    grid.setTargetDensity(target_density);   // filler sizing may have raised it

    reinitializeStdCells();
    resetSolverState();

    m_phase            = Phase::STDCELL_FIXED_MACRO;
    m_phase_start_iter = iteration;   // schedule warmups restart here; `iteration` keeps counting
    mixed_size_mode    = false;       // XPlace include_macros = False for the rest of the run

    // TODO #16: rebuildFillers() replaced the filler set, the second node-set change of this
    // transition. Opened here rather than at the rebuild so the generation is labelled with the
    // phase it actually belongs to.
    beginPositionDumpGeneration();

    // Re-derive the stop threshold from config rather than un-doubling it (Mark's call). XPlace
    // sets stop_overflow = args.stop_overflow * 2 only while include_macros holds, so phase 2
    // must see the plain configured value however phase 1 modified it.
    overflow_threshold = ConfigUtils::require<float>(cfg, "params", "convergence_overflow_threshold");

    Logger::log_info("Phase 2 (" + std::string(phaseName(m_phase)) + "): froze " +
                     std::to_string(frozen) + " macros, re-seeded " +
                     std::to_string(db.getFillerStartIndex()) + " movable cells, stop overflow " +
                     PREC(overflow_threshold));

    // Re-bootstrap the gradients and the density weight at the new positions. This is
    // performIterationZero() minus recordInitialHPWL(), which must keep reporting the RUN's
    // initial HPWL, not phase 2's.
    iterationReset();
    computeHpwlPartials();
    computeElectricFields();
    initializeDensityWeight();

    // ...and the re-seeded starting state, so the GIF shows what phase 2 actually begins from.
    dumpIterationPositions("reseeded");
    return true;
}

/**
 * @brief Stage 2 — remove macro overlap at minimum displacement.
 *
 * Deliberately separate from the transition: stage 3 (everything else in this file) is what
 * produces the overflow improvement, and it can be measured with the macros left exactly where
 * global placement put them. Legalization makes the result physically realisable.
 */
void Placer::legalizeMacros()
{
    if (!macro_legalization_enabled) {
        Logger::log_info("Macro legalization: disabled — macros frozen where GP left them "
                         "(overlapping; phase-2 overflow is still meaningful, the placement is not legal)");
        return;
    }
    runMacroLegalization();
}

/**
 * @brief Re-seed the standard cells, keeping the frozen macros where they are.
 *
 * XPlace: get_mov_node_info(init_method="randn_center") re-randomises EVERY movable cell to a
 * Gaussian at the die centre and then copies the legalised macro positions back over them
 * (database.py:889). Since we freeze first, "every movable cell" is already exactly the set we
 * want to move, so no copy-back is needed.
 *
 * The re-randomisation is not a detail — it is why phase 1's standard-cell placement is
 * discarded. Reusing it would start phase 2 inside phase 1's clumped local minimum.
 */
void Placer::reinitializeStdCells()
{
    Position die_centre(grid.getDieWidth() / 2.0f, grid.getDieHeight() / 2.0f);
    int seed = cfg["params"]["random_seed"].value_or(-1);
    // Offset the seed so phase 2 does not replay phase 1's exact draw, while staying
    // reproducible for a pinned random_seed.
    std::mt19937 gauss_gen(seed >= 0 ? (unsigned)seed + 1u : (unsigned)std::time(nullptr));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float sigma_x = grid.getDieWidth() * 0.001f;
    float sigma_y = grid.getDieHeight() * 0.001f;

    for (Component* comp_p : db.getMovableComponents())
        comp_p->initializeState(die_centre + Position(gauss(gauss_gen) * sigma_x,
                                                      gauss(gauss_gen) * sigma_y));

    // Fillers spread uniformly, as at setup: they stand for whitespace everywhere, so clustering
    // them at the centre would hide the vacant regions the cells must flow into.
    for (Component* filler_p : db.getFillers())
        filler_p->initializeState(Position(rand() % grid.getDieWidth(),
                                           rand() % grid.getDieHeight()));
}

/**
 * @brief Reset the optimizer for a fresh phase (XPlace "Reset optimizer...").
 *
 * Everything here is per-phase state. The iteration counter and the HPWL/overflow histories are
 * deliberately NOT reset: the trace stays continuous across the boundary, and the phase-relative
 * min_iterations floor keeps any convergence test from firing until the windows hold phase-2 data.
 */
void Placer::resetSolverState()
{
    nesterov_ak    = 1.0f;
    momentum_coeff = 0.0f;
    precond_coef   = 1.0f;
    // step_length is re-estimated at phaseIteration() == 1 by estimateInitialStep(), exactly as
    // it was for phase 1 — XPlace likewise re-runs estimate_initial_learning_rate at the restart.
    life                             = MAX_LIFE;
    convergence_iterations_remaining = -1;
    last_density_jolt_iter           = -1000;
    best_primary                     = BestSolution{};
    best_fallback                    = BestSolution{};
    m_stop_reason                    = StopReason::RUNNING;

    // gamma restarts high (overflow is ~1 again after the re-seed) so the WA surrogate is smooth
    // while the cells spread, exactly as at setup.
    gamma     = gamma_schedule ? 10.0f * base_gamma : base_gamma;
    inv_gamma = 1.0f / gamma;
    if (partials_method == "simple") initHpwlLut();
}

/**
 * @brief Emit a short report at a phase boundary, and stash phase 1's numbers for the final
 *        summary. Mark asked for this explicitly: without it a two-phase run reports only the
 *        phase-2 endpoint and the macro-placement quality phase 1 is responsible for is invisible.
 */
void Placer::reportPhaseSummary()
{
    float overflow_smoothed = computeOverflow(true,  nullptr, false);  // filler-EXCLUDED, as XPlace overflow_fn is
    float overflow_exact    = computeOverflow(false, nullptr, false);  // filler-EXCLUDED, as XPlace reports it
    float hpwl              = hpwl_history.empty() ? 0.0f : hpwl_history.back();

    m_phase1_summary = { true, iteration, hpwl, overflow_smoothed, overflow_exact, m_stop_reason };

    Logger::log_info("[PHASE] name=" + std::string(phaseName(m_phase)) +
                     " end_iteration=" + std::to_string(iteration) +
                     " reason=" + std::string(stopReasonName(m_stop_reason)) +
                     " hpwl=" + SCI(hpwl) +
                     " ovfw_smoothed=" + PREC(overflow_smoothed) +
                     " ovfw_exact=" + PREC(overflow_exact) +
                     " movable_macros=" + std::to_string(num_movable_macros));
}

AIEPLACE_NAMESPACE_END

# Completed Tasks — History

Cross-session task archive. Tasks move here after completion. See `TODO.md` for active work.

---

## #5 — Logger cleanup (DONE 2026-07-30 for sw_only)

**Summary:** Refactored sw_only's console logging and run-report system to match the governing principle:
the console shows the algorithm's user *useful* output and nothing more; nuisance detail belongs in a
report file. `interactive = false` → bare minimum. `quiet = true` → nothing but errors.

### Completed work

- [x] **Ordered severity scale** replaces the flat `Logger::keys` set (`Logger.h`): a single
      `LogLevel` threshold, `TRACE < DEBUG < DETAIL < ITER < INFO < WARNING < ERROR < CRITICAL`.
      Two orderings are deliberate and load-bearing, not alphabetical accidents:
      * `TRACE`/`DEBUG` sit **below** `DETAIL` because the run report captures `DETAIL`+, so the
        two developer-dump levels stay opt-in and never bloat it.
      * `ITER` sits just **below** `INFO` so `interactive=false` drops the live-status line while
        keeping every INFO message — one threshold, no second flag.
      Custom named channels (`profiling`) survive as an opt-in set alongside the scale, for output
      that isn't more or less severe than anything (`Logger::log_key`). `dbinfo` was dead — removed.
- [x] **Singleton + mutex dropped.** `iLogger`, `getLogger()`, `getMutex()` (declared, never
      defined), `iMutex` and the private ctor are gone; Logger is a plain static utility. This also
      removes the inconsistent `updateFunctionStats` lock. Revisit only if the merged host (#9) is
      actually threaded — then lock `function_stats_map` on *all three* accessors, not one.
      **UPDATE 2026-07-31 (#12):** sw_only IS threaded now. No race today — every `TIME_FUNCTION`/
      `TIME_BLOCK` sits at function or pass scope, outside every parallel region, and `Logger.h`
      now says so at the macro. But the "revisit if threaded" condition has been met, so the next
      person who wants a timer inside a parallel loop must add the lock (all three accessors) first.
- [x] **Renderer rewritten** (`Logger::emit`). Was: one `tabulate::Table` per log line. That padded
      every line to the cell width (trailing whitespace on all 130 lines of a piped log) and emitted
      **8 ANSI escapes per line** on a TTY. Now plain stream writes: zero trailing whitespace, one
      escape pair per line, and colour only when `isatty(stdout)`.
- [x] **Run report** — `<run_dir>/run.log`, everything at `DETAIL`+, written regardless of console
      verbosity. Lines logged before the run dir exists are held in a backlog and flushed by
      `Logger::openReport` (called from `createRunOutputStructure`).
- [x] **`interactive` follows the stream** — defaults to `isatty(stdout)`, so a piped/DSE run is
      automatically non-interactive. `output.interactive` in the config still forces either way; it
      is commented out in `run_config.toml` so the default applies.
- [x] **Message re-triage** — nuisance `INFO` → `DETAIL` across DataBase/Setup/Output/Schedule/
      Step/Partials/Visualizer. `logStepDiagnostics` was gated on the `DEBUG` key but logged its
      20 lines at `log_info` — exactly the drift the ordered scale prevents; now `log_debug`
      throughout. Deprecated-config notice promoted `INFO` → `WARNING`.
- [x] **Table consolidation** — the fixed/movable/filler counts were four loose load-time lines;
      they now live as rows in the Benchmark info table and the loose lines are `DETAIL`.
- [x] **`X=10` throttle** → config `output.iterations_per_status`.

**Verified on adaptec1 (25 iters):** quiet = 0 lines; piped = 84 (68 of them the two tables, 11 message
lines); TTY = 103 (adds banner + live status). `run.log` = 192 lines, 0 trailing-whitespace, 0 ANSI.
Config force-on-in-a-pipe and force-off-on-a-TTY both verified, as is a non-default cadence.

### Open follow-ups (still TODO, moved to active list)

- [ ] `host/src/pl_algo/{src/Logger.cpp,include/Logger.h}` still has the OLD singleton/tabulate
      Logger. The two copies have now diverged; fold this rewrite in when the hosts merge (#9).
- [ ] The two summary tables still render with a **double border** (`| +---+---+ |`) because the
      callers nest a `Table` inside a title-only outer `Table` (`DataBase::printInfo`,
      `Placer::exportSummaryReports`). Caller-side tabulate idiom, not the Logger. Flattening to one
      bordered table with a title row would drop a level of box-drawing noise.
- [ ] The welcome banner still goes straight to `cout` via `banner.print(cout)`, bypassing the
      Logger — it is the only remaining source of trailing whitespace (11 lines). It is terminal-only
      decoration now, so this is cosmetic.
- [ ] `run.log` is written for **every** run including DSE sweeps (quiet only silences the console).
      ~250 KB/run at 1200 iterations; a 500-run sweep adds ~125 MB. Gate on `quiet` if that bites —
      see TODO #1's results/ pruning.
- [ ] "Algorithm time (s) | 0.000" in the Run Statistics table looks wrong (`algo_time` never
      accumulated?). Noticed in passing, unrelated to logging — not investigated.

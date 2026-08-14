# rules.md — the non-negotiables

Hard rules. Violating one breaks correctness, the verification contract, reproducibility,
or the build. Injected into every session (SessionStart hook), so keep it short and true.

New rules arrive two ways: seeded here because they're already proven, or **promoted from
[[noteToSelf.md]]** once a note has collected 3+ dated "I relied on this again" timestamps.
If a rule stops being true, delete it — a stale rule here is believed.

## Environment
- **The `Bash` tool runs on Windows (Git Bash), not WSL. Wrap every command:**
  ```bash
  wsl -e bash -c "cd /home/msears/phd/AIEplace && <your command>"
  ```
  Bare commands hit the Windows filesystem and fail or do the wrong thing. Other WSL/Vitis
  friction points (background-run death, tmpfs wipe, freopen hang) live in [[noteToSelf.md]].

## Verification — a module isn't done until it's verified
- **Every PL module is verified offline against a golden before it goes near the device.**
  Run `cd vck5000 && make test` (tier-1, seconds) after **every** edit under
  `pl/src/pl_algo/src/modules/`. A block that hasn't cleared this isn't done; optimizing an
  unverified block wastes the effort.
- **Run `make test-regress` before AND after any change under `host/src/sw_only/`** — it
  asserts the trajectory + final-position hash are bit-identical to the committed baseline.
- **A test asserts; it does not print.** The harness computes the verdict itself and exits
  0 (pass) / non-zero (fail). This applies to **every** number it emits as evidence, not
  just the headline one — a printed number nobody `if`-checks is where the bug hides.

## Faithfulness to XPlace
- **Before inventing a heuristic, read how XPlace does it** — `grep -rn "<quantity>" ~/phd/Xplace/src/`.
  Match its formulation, or state explicitly that the choice is ours and write it down.
  Don't reason it out from first principles; don't guess from memory.
- **Same quantity → same name in sw_only and pl_algo**, with a comment at the declaration
  mapping to XPlace's symbol and its file (`// matches XPlace's weighted_weight (param_scheduler.py:386)`).
  Same name / different maths is invisible to diff, grep, and tests — it cost real time (#19b).

## Git
- **Commit only when Mark asks.** Then commit after each verified milestone, on a branch if
  on `main`.

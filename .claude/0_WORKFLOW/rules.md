# rules.md — the non-negotiables

Hard rules. Violating one breaks correctness, the verification contract, reproducibility,
or the build. Injected into every session (SessionStart hook), so keep it short and true.

New rules arrive two ways: seeded here because they're already proven, or **promoted from
[[noteToSelf.md]]** once a note has collected 3+ dated "I relied on this again" timestamps.
If a rule stops being true, flag it for deletion and tell Mark — a stale rule here is believed.

## Environment
- **The `Bash` tool runs on Windows (Git Bash), not WSL. Wrap every command:**
  ```bash
  wsl -e bash -c "cd /home/msears/phd/AIEplace && <your command>"
  ```
  Bare commands hit the Windows filesystem and fail or do the wrong thing. Other WSL/Vitis
  friction points (background-run death, tmpfs wipe, freopen hang) live in [[noteToSelf.md]].
- **Build server (`ssh build` → `hacc-build-01.inf.ethz.ch`, via the ETH jumphost).** Vitis/XRT and
  the VCK5000 card live here; the repo is at **`~/AIEplace`** (NOT `~/phd/AIEplace`). Every hop needs
  VPN + a password, so for non-interactive use open a multiplexed master **from WSL once** — `wsl ssh
  -fN build` (enter password), with `ControlMaster auto` / `ControlPath ~/.ssh/cm-%r@%h:%p` /
  `ControlPersist` set on `Host build` in `~/.ssh/config` — then later `ssh build` reuse it silently.
  Windows `ssh.exe` can't multiplex, so the master must be WSL-side.
  - Run remote commands as `ssh build bash --noprofile --norc -s` with the script on stdin — the
    server's `.bashrc` otherwise spews `module: command not found` into the output stream.
  - **No GitHub push creds on the server** (HTTPS remote, no helper, can't prompt). Don't push from it:
    commit there, then `git fetch build:AIEplace <branch>` into the laptop and push from the laptop.

## Verification — a module isn't done until it's verified
- **Every PL module is verified offline against a golden before it goes near the device.**
  Run `cd vck5000 && make test` (tier-1, seconds) after any major edits under
  `pl/src/pl_algo/src/modules/`. A block that hasn't cleared this isn't done; optimizing an
  unverified block wastes the effort.

- **A test asserts, printing only on failure.** The harness computes the verdict itself and exits
  0 (pass) / non-zero (fail). This applies to **every** number it emits as evidence, not
  just the headline one — a printed number nobody `if`-checks is where the bug hides.

- **Run the regression suite before writing a handoff or report** — `make test-regress` for
  sw_only changes, `cd vck5000 && make test` for pl_algo changes. A report's numbers should be
  backed by a passing regression at write time, not asserted from memory of an earlier run.

## Faithfulness to XPlace
- **Before inventing a heuristic, read how XPlace does it** — `grep -rn "<quantity>" ~/phd/Xplace/src/`.
  Match its formulation, or state explicitly that the choice is ours and write it down.
  Don't reason it out from first principles; don't guess from memory.
- **Same quantity → same name in sw_only and pl_algo.** In sw_only, comments
  map to XPlace's symbol and its file e.g. `// equal to XPlace's weighted_weight (param_scheduler.py:386)`.
  Analyzing the same name / different maths is invisible to diff, grep, and tests — it costs real time (#19b).

## Developing pl_algo
- Try to be faithful to sw_only, match functionality. It should match xPlace too by extension if uncertain.
- **sw_only is FROZEN (2026-08-18).** Do not change behaviour under `host/src/sw_only/` — bit-identical
  `make test-regress` is the contract, not a convenience. Cleanup, tooling, docs and tests are NOT
  frozen. Match the frozen sw_only; never "fix" a pl_algo mismatch by moving sw_only instead.

## Git
- **Commit when:** a report is written, a `HANDOFF_...` is converted into a `REPORT_...`, or
  Mark asks. Otherwise don't commit unprompted — these three triggers keep commits tied to
  verified checkpoints instead of firing on every small edit.
- Outside those triggers, prompt Mark to commit if you think it should happen (e.g. a major
  milestone not yet written up) — but wait for his yes.
#!/usr/bin/env python3
"""
Assert that a run's coord_dump/ is internally consistent and says what it claims (TODO #16).

    python3 tools/check_viz_dump.py <run_dir> [<run_dir> ...]

Exits 0 if every check passes, non-zero on the first failure, and prints only failures plus a
one-line summary. This is a TEST, not a report: every number it looks at is compared in code.

Why it exists. Every channel in the dump is indexed by frame number, so a stream of the wrong
length does not fail loudly -- it silently returns a neighbouring frame, or half of two, and the
GIF still renders. The same is true of a wrong record stride inside a frame. The placer checks its
own stream lengths at finalize; this checks the same thing from the READER's side, where a
misunderstanding of the format shows up, and then checks invariants the placer cannot check about
itself.

The four structural invariants, each of which catches a misaligned record rather than a wrong
value -- misalignment being the realistic failure mode for a binary wire format:

  1. fillers carry net_degree == 0            (they are generated whitespace, on no net)
  2. fillers carry a wirelength gradient of exactly 0.0
                                              (Partials.cpp clears probe_grad every iteration and
                                               no net ever adds to a filler's)
  3. precond_weight >= 1.0 everywhere         (updatePrecondWeights: max(1.0f, pins + lambda*area),
                                               and exactly 1.0 when preconditioning is off)
  4. total deposited density is constant within a generation
                                              (the deposit is area-conserving and die boundaries
                                               are enforced, so mass cannot leave the map)

Any of 1-3 failing on a misaligned stream is near-certain; all three passing on a misaligned one
is not credible.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

# Kind byte from PositionDump.cpp. Only the filler kind is load-bearing here.
K_FILLER = 4

# numpy type code per the manifest's dtype strings ("f4 x", "u1 kind", ...).
NP_CODE = {"f4": "<f4", "u4": "<u4", "u2": "<u2", "u1": "u1"}

# Relative spread allowed in the per-frame density mass within one generation. The deposit
# conserves area exactly in real arithmetic; this is float32 accumulation over up to 4M bins, so
# the bound is loose enough to ignore rounding and orders of magnitude tighter than any real leak.
DENSITY_MASS_TOL = 1e-3


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


def static_dtype(manifest):
    """numpy dtype for nodes_gen<N>.bin, built FROM THE MANIFEST rather than hard-coded.

    The record grew a field in format v2 (net_degree). Deriving it here is what lets one reader
    handle both versions, and it means the manifest is load-bearing rather than decorative.
    """
    fields = []
    for entry in manifest["static_record"]:
        code, name = entry.split()
        check(code in NP_CODE, f"unknown dtype code {code!r} in manifest static_record")
        fields.append((name, NP_CODE[code]))
    return np.dtype(fields)


def stream_len(path, expect, label):
    check(path.is_file(), f"{label}: {path.name} is declared in the manifest but missing")
    actual = path.stat().st_size
    check(actual == expect,
          f"{label}: {path.name} is {actual} bytes, expected {expect} -- this channel is out of "
          f"lockstep with frames_gen<N>.bin, so a frame index into it reads the wrong frame")


def channel_present(manifest, name):
    """Whether this run wrote a given optional channel. Absent from a v1 manifest entirely."""
    return bool(manifest.get("channels", {}).get(name, {}).get("present", False))


def check_generation(dump, manifest, gen):
    gid = gen["id"]
    frames = len(gen["frame_iters"])
    frame_nodes = gen["frame_nodes"]
    filler_start = gen["filler_start"]
    sdtype = static_dtype(manifest)
    tag = f"gen{gid}"

    # ---- static records ---------------------------------------------------------------------
    stream_len(dump / f"nodes_gen{gid}.bin", gen["num_static_nodes"] * sdtype.itemsize,
               f"{tag} static")
    static = np.fromfile(dump / f"nodes_gen{gid}.bin", dtype=sdtype)
    kinds = set(int(k) for k in manifest["kinds"])
    check(set(np.unique(static["kind"])).issubset(kinds),
          f"{tag}: static records carry kind bytes outside the manifest's kind map")
    check(np.all(np.isfinite(static["w"])) and np.all(static["w"] >= 0.0),
          f"{tag}: static record has a negative or non-finite width")
    check(np.all(np.isfinite(static["h"])) and np.all(static["h"] >= 0.0),
          f"{tag}: static record has a negative or non-finite height")

    if "net_degree" in sdtype.names:
        # Invariant 1. Fillers are generated whitespace and sit on no net; anything else means the
        # record is misaligned or the filler index range is wrong.
        fillers = static["kind"] == K_FILLER
        if fillers.any():
            check(np.all(static["net_degree"][fillers] == 0),
                  f"{tag}: {int((static['net_degree'][fillers] != 0).sum())} filler(s) carry a "
                  f"non-zero net degree -- the static record is misaligned")
        real = static["net_degree"][~fillers]
        check(real.size == 0 or real.max() > 0,
              f"{tag}: no node in the design is on any net -- net_degree is not being populated")

    # ---- position streams -------------------------------------------------------------------
    pos_bytes = frames * frame_nodes * 4
    stream_len(dump / f"frames_gen{gid}.bin", pos_bytes, f"{tag} frames")

    committed = np.fromfile(dump / f"frames_gen{gid}.bin",
                            dtype="<u2").reshape(frames, frame_nodes, 2)
    if channel_present(manifest, "probe"):
        stream_len(dump / f"probe_gen{gid}.bin", pos_bytes, f"{tag} probe")
        probe = np.fromfile(dump / f"probe_gen{gid}.bin",
                            dtype="<u2").reshape(frames, frame_nodes, 2)
        # A probe channel that is byte-identical to the committed one is the failure this catches:
        # it means v_k was never read and the file is an expensive copy of u_k. Momentum makes
        # v != u from the second iteration on, so a whole generation matching is not plausible.
        check(frames < 2 or not np.array_equal(committed, probe),
              f"{tag}: the probe channel is byte-identical to the committed positions across all "
              f"{frames} frames -- v_k is not being read")

    # ---- per-frame validity bits ------------------------------------------------------------
    valid = gen.get("frame_valid")
    if valid is not None:
        check(len(valid) == frames,
              f"{tag}: frame_valid has {len(valid)} entries for {frames} frames")
        # Every UNTAGGED frame is taken straight after a full gradient evaluation, so both
        # auxiliary channels describe it. The three tagged frames each break one or both, and
        # which one is not incidental -- it is the reason frame_valid exists. Asserted rather
        # than assumed so that a change to the phase-2 boundary has to come here and say so.
        expected_for_tag = {"legalized": 0, "reseeded": 1, "best_solution": 0}
        want_all = (1 if channel_present(manifest, "density") else 0) | \
                   (2 if channel_present(manifest, "forces") else 0)
        for i, (frame_tag, bits) in enumerate(zip(gen["frame_tags"], valid)):
            if not frame_tag:
                check(bits == want_all,
                      f"{tag} frame {i} (iter {gen['frame_iters'][i]}) is untagged but its "
                      f"validity bits are {bits}, expected {want_all}")
            elif frame_tag in expected_for_tag:
                check(bits == (expected_for_tag[frame_tag] & want_all),
                      f"{tag} frame {i} tagged {frame_tag!r} has validity bits {bits}, expected "
                      f"{expected_for_tag[frame_tag] & want_all} -- see PositionDump.cpp")

    # ---- density / field --------------------------------------------------------------------
    if channel_present(manifest, "density"):
        viz = manifest["viz_grid"]
        bins = viz["nx"] * viz["ny"]
        stream_len(dump / f"density_gen{gid}.bin", frames * bins * 4, f"{tag} density")
        rho = np.fromfile(dump / f"density_gen{gid}.bin",
                          dtype="<f4").reshape(frames, viz["ny"], viz["nx"])
        check(np.all(np.isfinite(rho)), f"{tag}: density map contains NaN or inf")
        check(np.all(rho >= 0.0), f"{tag}: density map contains a negative bin")

        # Invariant 4, over the frames whose density record was actually measured.
        measured = [i for i in range(frames) if valid is None or (valid[i] & 1)]
        if len(measured) > 1:
            mass = rho[measured].sum(axis=(1, 2)).astype(np.float64)
            spread = (mass.max() - mass.min()) / max(mass.mean(), 1e-30)
            check(spread <= DENSITY_MASS_TOL,
                  f"{tag}: total deposited density varies by {spread:.3e} across the generation "
                  f"(min {mass.min():.6e}, max {mass.max():.6e}); the deposit is area-conserving "
                  f"and die boundaries are enforced, so mass should not move")

        if channel_present(manifest, "field"):
            stream_len(dump / f"field_gen{gid}.bin", frames * bins * 8, f"{tag} field")
            efield = np.fromfile(dump / f"field_gen{gid}.bin", dtype="<f4")
            check(np.all(np.isfinite(efield)), f"{tag}: field map contains NaN or inf")

    # ---- forces -----------------------------------------------------------------------------
    if channel_present(manifest, "forces"):
        per_node = len(manifest["channels"]["forces"]["record"])
        stream_len(dump / f"forces_gen{gid}.bin", frames * frame_nodes * per_node * 4,
                   f"{tag} forces")
        forces = np.fromfile(dump / f"forces_gen{gid}.bin",
                             dtype="<f4").reshape(frames, frame_nodes, per_node)
        measured = [i for i in range(frames) if valid is None or (valid[i] & 2)]
        if measured:
            live = forces[measured]
            check(np.all(np.isfinite(live)), f"{tag}: force record contains NaN or inf")
            # Invariant 3.
            precond = live[:, :, 4]
            check(np.all(precond >= 1.0),
                  f"{tag}: preconditioner value below 1.0 (min {precond.min():.6e}); "
                  f"updatePrecondWeights clamps it to max(1.0, ...), so the record is misaligned")
            # Invariant 2. The strongest stride check available: it fixes both the record width
            # and where the fillers start.
            if filler_start < frame_nodes:
                filler_wl = live[:, filler_start:, 0:2]
                check(np.all(filler_wl == 0.0),
                      f"{tag}: {int((filler_wl != 0.0).sum())} filler wirelength-gradient "
                      f"components are non-zero -- fillers are on no net, so the force record "
                      f"stride or filler_start is wrong")
            # A frame in which nothing pulls is not a placement iteration.
            check(np.abs(live[:, :, 0:4]).sum() > 0.0,
                  f"{tag}: every force component in every measured frame is zero")

    return frames


def check_run(run_dir):
    dump = run_dir / "coord_dump"
    check(dump.is_dir(), f"{dump} not found -- was the run made with output.dump_positions = true?")
    manifest = json.loads((dump / "manifest.json").read_text())
    version = manifest.get("format_version", 0)
    check(version >= 1, f"unsupported coord_dump format_version {version}")

    total = 0
    for gen in manifest["generations"]:
        total += check_generation(dump, manifest, gen)

    channels = [name for name in ("probe", "density", "field", "forces")
                if channel_present(manifest, name)]
    return version, total, len(manifest["generations"]), channels


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__.strip().splitlines()[2].strip())

    failures = 0
    for arg in argv[1:]:
        run_dir = Path(arg)
        try:
            version, frames, gens, channels = check_run(run_dir)
        except Failure as failure:
            print(f"FAIL {run_dir}: {failure}")
            failures += 1
        except (OSError, ValueError, KeyError) as error:
            print(f"FAIL {run_dir}: dump is unreadable ({type(error).__name__}: {error})")
            failures += 1
        else:
            print(f"PASS {run_dir}: v{version}, {frames} frames in {gens} generation(s), "
                  f"channels u_k+{'+'.join(channels) if channels else '(none)'}")

    if failures:
        print(f"\nFAIL: {failures} of {len(argv) - 1} dump(s) failed.")
        return 1
    print(f"\nPASS: {len(argv) - 1} dump(s) consistent.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

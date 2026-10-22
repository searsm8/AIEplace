# AIEplace C++ host code

Host code to run the AIEplace algorithm: read the design, run (or drive) the placement iteration,
write the result. Two variants under `./src`, selected with `HOST=`:

| | |
|---|---|
| `src/sw_only` | the CPU-only golden reference (default) |
| `src/pl_algo` | the lean driver around the VCK5000 PL |
| `src/common` | parser + data model, built into **both** — see its README |

## First time on a fresh clone

Nothing to fetch: `cd vck5000 && make host HOST=sw_only` builds straight from `git clone`.

### Third-party code

| path | upstream | pinned at | needed by |
|---|---|---|---|
| `third_party/tabulate` | [p-ranav/tabulate](https://github.com/p-ranav/tabulate) — header-only tables, used by `Logger` | `3a58301`, **vendored** | the host |

tabulate's headers and licenses are checked in (see `third_party/tabulate/README.md`), so a plain
clone builds. It used to be a submodule, which left the directory empty until someone ran the
bootstrap script.

The design files are read by the host's own reader, `src/common/src/DesignReader.cpp` (TODO #43).
It replaced the Limbo parsers; Limbo itself was removed on 2026-10-22 (see *Limbo* below).

(`vck5000/aie/lib/Vitis_Libraries` is also a submodule, only needed for AIE builds and
**gigabytes** — nothing initializes it automatically. Do that by hand when you need it:
`git submodule update --init vck5000/aie/lib/Vitis_Libraries`.)

The host is built with the default (C++11) `std::string` ABI. It used to need
`-D_GLIBCXX_USE_CXX11_ABI=0` everywhere — and a per-file exception for pl_algo's XRT driver,
since libxrt uses the new ABI — only because Limbo's archives were built with the old one.

## Limbo — removed

Limbo (LEF/DEF/bookshelf parsers, submodule at tag `3.5.2`) was the host's parser until TODO #43,
then only the reference `vck5000/test/parser` checked the native reader against. On 2026-10-22 its
output for all 86 test inputs was frozen as hashes (`vck5000/test/parser/limbo_dump_hashes.txt`)
and the submodule, its out-of-tree build and `bootstrap_third_party.sh` were deleted. With it went
the reason for the two-Boost and old-string-ABI notes that used to live here.

To get Limbo back (e.g. to diff a failing input), check out `f932c98`, the last commit that has
it; this README and `vck5000/test/parser/README.md` there describe the build.

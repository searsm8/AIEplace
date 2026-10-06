# AIEplace C++ host code

Host code to run the AIEplace algorithm: read the design, run (or drive) the placement iteration,
write the result. Two variants under `./src`, selected with `HOST=`:

| | |
|---|---|
| `src/sw_only` | the CPU-only golden reference (default) |
| `src/pl_algo` | the lean driver around the VCK5000 PL |
| `src/common` | parser + data model, built into **both** — see its README |

## First time on a fresh clone

```
bash vck5000/tools/bootstrap_third_party.sh
```

Then `cd vck5000 && make host HOST=sw_only`.

Or clone with submodules already populated:

```
git clone --recurse-submodules <url>
```

### Why the bootstrap step exists

The host's one third-party dependency is a **git submodule**, not a copy of someone else's source
checked in here:

| submodule | upstream | pinned at | needed by |
|---|---|---|---|
| `third_party/tabulate` | [p-ranav/tabulate](https://github.com/p-ranav/tabulate) — header-only tables, used by `Logger` | `3a58301` | the host |
| `third_party/Limbo` | [limbo018/Limbo](https://github.com/limbo018/Limbo) — LEF/DEF/bookshelf parsers | tag `3.5.2` (`81b64433`) | **only** `vck5000/test/parser` |

The design files are read by the host's own reader, `src/common/src/DesignReader.cpp` (TODO #43).
Limbo stays only as the reference that reader is checked against — see *Limbo* below.

(A third submodule, `vck5000/aie/lib/Vitis_Libraries`, is only needed for AIE builds and is
**gigabytes** — the bootstrap script deliberately does *not* initialize it. Do that by hand when
you need it: `git submodule update --init vck5000/aie/lib/Vitis_Libraries`.)

A submodule is a pointer, not a copy: this repo stores only the URL (in `.gitmodules`) and one
commit id. A plain `git clone` therefore leaves the directory **empty**, and the build fails on a
missing `tabulate/table.hpp` until you populate it. That is all the bootstrap script does by
default:

```
git submodule update --init third_party/tabulate
```

Note it names the submodule rather than using `--recursive`, which would also drag in the
multi-gigabyte Vitis_Libraries. tabulate is header-only, so there is nothing to compile.

The host is built with the default (C++11) `std::string` ABI. It used to need
`-D_GLIBCXX_USE_CXX11_ABI=0` everywhere — and a per-file exception for pl_algo's XRT driver,
since libxrt uses the new ABI — only because Limbo's archives were built with the old one.

## Limbo — only for the parser equivalence harness

`vck5000/test/parser` parses every benchmark with both the native reader and Limbo, and requires a
canonical dump of the parsed `DataBase` to be byte-identical (see its README). That needs Limbo
built:

```
bash vck5000/tools/bootstrap_third_party.sh --with-limbo
```

Limbo is a source library, and no prebuilt `.a` is stored in this repo (there used to be 22 MB of
them in three duplicate places). The build is deliberately **out of tree**:

| | |
|---|---|
| `third_party/Limbo/` | the submodule checkout — source only, stays byte-for-byte pristine |
| `third_party/limbo_build/` | CMake objects (gitignored) |
| `third_party/limbo_install/` | the collected `.a` (gitignored) |

so the submodule can never show a spurious diff and `git status` stays quiet. The harness takes
headers from the checkout (`-I third_party/Limbo`) and libraries from the install dir
(`-L third_party/limbo_install/lib`).

The equivalent by hand, if you would rather see the steps:

```
git submodule update --init third_party/Limbo
cmake -S third_party/Limbo -B third_party/limbo_build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$PWD/third_party/limbo_install \
      -DBoost_NO_BOOST_CMAKE=ON
make -C third_party/limbo_build -j$(nproc) && make -C third_party/limbo_build install
```

Two flags there are load-bearing:

- **`-DBoost_NO_BOOST_CMAKE=ON`** — see the Boost section below.
- **the ABI** — Limbo's CMake defaults `CMAKE_CXX_ABI` to `0`, i.e. `-D_GLIBCXX_USE_CXX11_ABI=0`,
  which is why the harness's Limbo binary (`parse_bench_limbo`) and every object in it are
  compiled with that same define. Do not override it on either side independently, or the link
  fails on `std::__cxx11::basic_string` symbols.

### Boost — read this before debugging a Boost problem

Boost reaches this project only through Limbo, so this matters only for `--with-limbo`.

**This box has two Boost installations and they do not agree.** Established 2026-08-05:

| | version | contents |
|---|---|---|
| `/usr/include` | **1.71** (apt) | complete: headers + every `libboost_*.so` |
| `/usr/local/include` | **1.80** (built from source) | headers complete, but only *some* `.so` — `iostreams`, `serialization`, `system`, `thread`, `test`. **No `graph`, no `regex`.** |

gcc searches `/usr/local/include` **before** `/usr/include`, so every `#include <boost/...>`
resolves to **1.80**, and no `-I` can change that (gcc de-duplicates `-I` against its own system
directories). Two consequences:

- `-DBoost_NO_BOOST_CMAKE=ON` is required when configuring Limbo. Without it CMake's *config*
  mode finds `/usr/local/lib/cmake/Boost-1.80.0/BoostConfig.cmake`, which advertises 1.80, then
  fails looking for a `boost_graph` 1.80 component that was never installed:
  `Could not find a configuration file for package "boost_graph" ... version "1.80.0"`.
- With that flag, CMake's *module* mode resolves headers to `/usr/local/include` (1.80) but
  `boost_graph`/`boost_regex` to the **1.71** `.so` in `/usr/lib/x86_64-linux-gnu`. Compiling
  against 1.80 headers and linking 1.71 libraries is a genuine ABI bug.

**Why that bug does not bite**: the four Limbo archives the harness links (`lefparseradapt`,
`defparseradapt`, `bookshelfparser`, `gzstream`) have **zero** undefined `boost::` symbols — the
mismatch is confined to Limbo targets we never build. `bootstrap_third_party.sh --with-limbo`
**asserts that on every run**, along with the host's and Limbo's Boost versions agreeing. If it
ever complains, the fix is a decision about this machine — either complete the 1.80 install
(`graph`, `regex`) or remove it so the complete 1.71 wins — not a flag in this repo.

### Updating Limbo later

```
cd third_party/Limbo && git fetch && git checkout <new-tag>
cd - && git add third_party/Limbo      # records the new commit id in AIEplace
bash vck5000/tools/bootstrap_third_party.sh --with-limbo --clean
```

The `git add` is the part people miss: the superproject tracks *which commit* the submodule is
at, so moving the submodule and not committing that pointer leaves everyone else on the old one.

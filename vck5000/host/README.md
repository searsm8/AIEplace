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

Two dependencies are **git submodules**, not copies of someone else's source checked in here:

| submodule | upstream | pinned at |
|---|---|---|
| `third_party/Limbo` | [limbo018/Limbo](https://github.com/limbo018/Limbo) — LEF/DEF/bookshelf parsers | tag `3.5.2` (`81b64433`) |
| `third_party/tabulate` | [p-ranav/tabulate](https://github.com/p-ranav/tabulate) — header-only tables, used by `Logger` | `3a58301` |

(A third, `vck5000/aie/lib/Vitis_Libraries`, is only needed for AIE builds and is **gigabytes** —
the bootstrap script deliberately does *not* initialize it. Do that by hand when you need it:
`git submodule update --init vck5000/aie/lib/Vitis_Libraries`.)

A submodule is a pointer, not a copy: this repo stores only the URL (in `.gitmodules`) and one
commit id. A plain `git clone` therefore leaves both directories **empty**, and the build fails
on missing `limbo/parsers/...` and `tabulate/table.hpp` headers until you populate them. That is
the bootstrap script's first step:

```
git submodule update --init third_party/Limbo third_party/tabulate
```

Note it names them rather than using `--recursive`, which would also drag in the multi-gigabyte
Vitis_Libraries.

Limbo also has to be **compiled** (tabulate is header-only, so cloning it is enough) — it is a source library, and no prebuilt `.a` is stored in
this repo (there used to be 22 MB of them in three duplicate places). The build is deliberately
**out of tree**:

| | |
|---|---|
| `third_party/Limbo/` | the submodule checkout — source only, stays byte-for-byte pristine |
| `third_party/limbo_build/` | CMake objects (gitignored) |
| `third_party/limbo_install/` | the collected `.a` (gitignored) |

so the submodule can never show a spurious diff and `git status` stays quiet. The host takes
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
  which is why every host TU is compiled with that same define. Do not override it on either
  side independently, or the link fails on `std::__cxx11::basic_string` symbols.

## Boost — read this before debugging a Boost problem

**This box has two Boost installations and they do not agree.** Established 2026-08-05:

| | version | contents |
|---|---|---|
| `/usr/include` | **1.71** (apt) | complete: headers + every `libboost_*.so` |
| `/usr/local/include` | **1.80** (built from source) | headers complete, but only *some* `.so` — `iostreams`, `serialization`, `system`, `thread`, `test`. **No `graph`, no `regex`.** |

gcc searches `/usr/local/include` **before** `/usr/include`, so every `#include <boost/...>` in
this project resolves to **1.80**, and no `-I` can change that (gcc de-duplicates `-I` against
its own system directories). Two consequences:

- `-DBoost_NO_BOOST_CMAKE=ON` is required when configuring Limbo. Without it CMake's *config*
  mode finds `/usr/local/lib/cmake/Boost-1.80.0/BoostConfig.cmake`, which advertises 1.80, then
  fails looking for a `boost_graph` 1.80 component that was never installed:
  `Could not find a configuration file for package "boost_graph" ... version "1.80.0"`.
- With that flag, CMake's *module* mode resolves headers to `/usr/local/include` (1.80) but
  `boost_graph`/`boost_regex` to the **1.71** `.so` in `/usr/lib/x86_64-linux-gnu`. Compiling
  against 1.80 headers and linking 1.71 libraries is a genuine ABI bug.

**Why that bug does not currently bite us**, and how it is kept that way: Boost is *header-only*
across everything AIEplace links. The four Limbo archives we use (`lefparseradapt`,
`defparseradapt`, `bookshelfparser`, `gzstream`) have **zero** undefined `boost::` symbols and
the host binary links no `libboost` at all — the mismatch is confined to Limbo targets we never
build. `bootstrap_third_party.sh` **asserts all three of those conditions on every run**: host
Boost version, Limbo's configured Boost version, and the zero-compiled-Boost-symbols property.
If it ever complains, the fix is a decision about this machine — either complete the 1.80 install
(`graph`, `regex`) or remove it so the complete 1.71 wins — not a flag in this repo.

There used to be a `-I$HOME/local/boost_1_82_0/` in `src/sw_only/makeflags.mk`. That directory
does not exist; the flag did nothing and implied a third version. It is gone.

### Updating Limbo later

```
cd third_party/Limbo && git fetch && git checkout <new-tag>
cd - && git add third_party/Limbo      # records the new commit id in AIEplace
bash vck5000/tools/bootstrap_third_party.sh --clean
```

The `git add` is the part people miss: the superproject tracks *which commit* the submodule is
at, so moving the submodule and not committing that pointer leaves everyone else on the old one.

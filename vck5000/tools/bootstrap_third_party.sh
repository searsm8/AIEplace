#!/usr/bin/env bash
# bootstrap_third_party.sh -- fetch and build the third-party dependencies a fresh clone needs.
#
# Run ONCE after cloning, before the first `make host`:
#     bash vck5000/tools/bootstrap_third_party.sh
#
# What it does, and why:
#   Limbo (the LEF/DEF/bookshelf parser library) is a git SUBMODULE, not a copy of someone
#   else's source checked into this repo. A submodule stores only a URL (.gitmodules) plus one
#   commit id, so `git clone` of AIEplace gives you an EMPTY third_party/Limbo directory until
#   somebody runs `git submodule update --init`. This script does that, then builds Limbo.
#
#   The build is deliberately OUT OF TREE -- third_party/limbo_build (objects) and
#   third_party/limbo_install (the collected .a) -- so the submodule checkout itself stays
#   byte-for-byte pristine and can never show a spurious diff. Both directories are gitignored
#   by the superproject. The host takes HEADERS from the submodule source tree
#   (-I third_party/Limbo) and LIBS from the install dir (-L third_party/limbo_install/lib).
#
# Re-running is safe: the submodule sync is idempotent and the Limbo build is incremental.
# Pass --clean to force a full Limbo rebuild.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LIMBO_DIR="$REPO_ROOT/third_party/Limbo"
BUILD_DIR="$REPO_ROOT/third_party/limbo_build"
INSTALL_DIR="$REPO_ROOT/third_party/limbo_install"
JOBS=${JOBS:-$(nproc)}
CLEAN=0
[[ ${1:-} == "--clean" ]] && CLEAN=1

echo "== repo: $REPO_ROOT"

# ---- 1. fetch the submodules at their pinned commits ---------------------------------------
# Named explicitly, NOT `--init --recursive`: vck5000/aie/lib/Vitis_Libraries is also a
# submodule and is gigabytes. It is only needed for AIE builds -- init it by hand when you
# need it (`git submodule update --init vck5000/aie/lib/Vitis_Libraries`).
echo "== git submodule update --init (Limbo, tabulate)"
git -C "$REPO_ROOT" submodule update --init third_party/Limbo third_party/tabulate
git -C "$REPO_ROOT" submodule status third_party/Limbo third_party/tabulate

for probe_path in "$LIMBO_DIR/CMakeLists.txt" "$REPO_ROOT/third_party/tabulate/include/tabulate/table.hpp"; do
    if [[ ! -f "$probe_path" ]]; then
        echo "ERROR: missing $probe_path -- a submodule is still empty after the update." >&2
        exit 1
    fi
done

# ---- 2. build + install it in place -------------------------------------------------------
# -DBoost_NO_BOOST_CMAKE=ON is REQUIRED on this box, not a preference: there is a stray
# /usr/local/lib/cmake/Boost-1.80.0/BoostConfig.cmake that advertises 1.80 while the actual
# system Boost is 1.71. CMake's config mode finds the 1.80 config first and then fails with
# "Could not find a configuration file for package boost_graph ... version 1.80.0". This flag
# forces the classic FindBoost module, which finds the real headers under /usr/include.
#
# CMAKE_CXX_ABI defaults to 0 in Limbo's own CMakeLists, i.e. -D_GLIBCXX_USE_CXX11_ABI=0 --
# the same old ABI the host is compiled with. Do not override it, or the host will fail to
# link with undefined std::__cxx11::basic_string symbols.
if [[ $CLEAN == 1 ]]; then
    echo "== --clean: removing previous Limbo build output"
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
fi

echo "== cmake Limbo -> $BUILD_DIR (install to $INSTALL_DIR)"
mkdir -p "$BUILD_DIR"
cmake -S "$LIMBO_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DBoost_NO_BOOST_CMAKE=ON > "$BUILD_DIR/bootstrap_cmake.log" 2>&1   # 2>&1: lemon's CMake
                                                # deprecation warnings are noise, not our problem

echo "== building Limbo (-j$JOBS); this takes a few minutes"
make -C "$BUILD_DIR" -j"$JOBS" > "$BUILD_DIR/bootstrap_build.log" 2>&1
make -C "$BUILD_DIR" install > "$BUILD_DIR/bootstrap_install.log" 2>&1

# ---- 3. check the host actually got what it links against ---------------------------------
missing=0
for lib in liblefparseradapt.a libdefparseradapt.a libbookshelfparser.a libgzstream.a; do
    if [[ ! -f "$INSTALL_DIR/lib/$lib" ]]; then
        echo "MISSING: third_party/limbo_install/lib/$lib" >&2
        missing=1
    fi
done
if [[ $missing == 1 ]]; then
    echo "Limbo build did not produce every library the host links. See $BUILD_DIR/bootstrap_build.log" >&2
    exit 1
fi

# The submodule checkout must be untouched by all of the above -- that is the point of building
# out of tree, and it is cheap to assert rather than trust.
dirty=$(git -C "$LIMBO_DIR" status --porcelain | wc -l)
if [[ $dirty -ne 0 ]]; then
    echo "WARNING: the Limbo submodule is no longer pristine ($dirty entries):" >&2
    git -C "$LIMBO_DIR" status --short >&2
fi

# ---- 4. Boost reconciliation -------------------------------------------------------------
# This box has TWO Boosts and they do not agree, so the situation is asserted rather than
# assumed. Established 2026-08-05:
#   /usr/include        Boost 1.71 from apt, complete (headers + all .so)
#   /usr/local/include  Boost 1.80 built from source, headers complete but only SOME .so
#                       (iostreams, serialization, system, thread, test -- no graph, no regex)
# gcc searches /usr/local/include first, so every #include <boost/...> in this project gets
# 1.80, while Limbo's CMake resolves boost_graph/boost_regex to the 1.71 .so in
# /usr/lib/x86_64-linux-gnu. Compiling against 1.80 headers and linking 1.71 libraries is a
# genuine ABI bug -- it is harmless HERE only because it is confined to Limbo targets AIEplace
# never builds, and because Boost is header-only across everything we do link.
#
# The three checks below are exactly the conditions that make it harmless. If one fails, the
# mismatch has stopped being confined and the fix is a system decision (complete the 1.80
# install, or remove it and let 1.71 win), not a flag in this repo.
echo
echo "== Boost consistency check"
probe=$(mktemp -d)
cat > "$probe/probe.cpp" <<'EOF'
#include <boost/version.hpp>
#include <cstdio>
int main() { printf("%s\n", BOOST_LIB_VERSION); return 0; }
EOF
# (a) which Boost the HOST will compile against -- same search path the host build uses.
#     NB the preprocessor output goes to a FILE, not a pipe: `g++ -E | grep -m1` makes grep
#     exit first, g++ take SIGPIPE, and `set -o pipefail` abort the whole script.
g++ -std=c++2a -o "$probe/probe" "$probe/probe.cpp"
host_boost=$("$probe/probe")
g++ -E "$probe/probe.cpp" > "$probe/probe.i"
host_boost_hdr=$(grep -o '"[^"]*boost/version.hpp"' "$probe/probe.i" | head -1 | tr -d '"')
echo "   host compiles against Boost $host_boost   ($host_boost_hdr)"

# (b) which Boost headers Limbo was configured with -- must be the same or the archives we
#     link were built against a different Boost than the code calling them.
limbo_inc=$(grep '^Boost_INCLUDE_DIR:' "$BUILD_DIR/CMakeCache.txt" | head -1 | cut -d= -f2)
limbo_boost=$(grep '#define BOOST_LIB_VERSION' "$limbo_inc/boost/version.hpp" | head -1 | tr -d '"' | awk '{print $3}')
echo "   Limbo built against Boost $limbo_boost   ($limbo_inc)"
if [[ "$host_boost" != "$limbo_boost" ]]; then
    echo "   MISMATCH: host uses $host_boost, Limbo's archives were built with $limbo_boost." >&2
    echo "   Boost is header-only across this project, so differing versions mean the parser" >&2
    echo "   archives and the code calling them disagree on inline definitions (ODR)." >&2
    exit 1
fi

# (c) the archives the host links must need NO compiled Boost. This is the property that makes
#     the 1.80-headers / 1.71-.so split irrelevant to us; assert it instead of believing it.
boost_syms=0
for lib in liblefparseradapt libdefparseradapt libbookshelfparser libgzstream; do
    n=$(nm -C --undefined-only "$INSTALL_DIR/lib/$lib.a" 2>/dev/null | grep -c 'boost::' || true)
    boost_syms=$((boost_syms + n))
done
if [[ $boost_syms -ne 0 ]]; then
    echo "   WARNING: the linked Limbo archives now reference $boost_syms compiled boost:: symbols." >&2
    echo "   The header/library version split above is no longer harmless -- see the comment" >&2
    echo "   in this script and in host/src/sw_only/makeflags.mk." >&2
else
    echo "   linked Limbo archives need no compiled Boost (0 undefined boost:: symbols) -- OK"
fi
rm -rf "$probe"

echo
echo "== OK. Limbo libs installed to third_party/limbo_install/lib:"
ls -1 "$INSTALL_DIR/lib" | sed 's/^/     /' | head -30
echo
echo "Now:  cd vck5000 && make host HOST=sw_only"

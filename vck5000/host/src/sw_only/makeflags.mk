
# sw_only is the CPU-only golden reference by design -- there is no XRT/VCK5000 path here.
# Hardware offload lives in the pl_algo variant (HOST=pl_algo, which honors BUILD_XRT).
# See TODO #9: the two hosts merge once pl_algo bring-up completes.
BUILD_VIZ ?= 1# Set to 1 to build with visualization support.

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = placer/AIEplace.cpp placer/Setup.cpp placer/Schedule.cpp placer/Step.cpp \
	    placer/Partials.cpp placer/Density.cpp placer/Output.cpp \
	    placer/Phase2.cpp placer/MacroLegalize.cpp DCT.cpp
# Parser + data model, shared with pl_algo -- see host/src/common (TODO #9).
COMMON_SRCS = DataBase.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o) $(COMMON_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# General
CPPFLAGS += -I $(HOST_DIR)/include
CPPFLAGS += -I $(HOST_COMMON_DIR)/include

CXXFLAGS += -std=c++2a
CXXFLAGS += -g
#CXXFLAGS += -O0 # optimization level, 0 means no optimization
CXXFLAGS += -O2 # optimization level (experiment: was -O0; perturbs golden low bits)
#CXXFLAGS += -Wall

# OpenMP: the placement iteration is threaded over nodes/nets/grid rows (TODO #12). Thread
# count is OpenMP's default (every core) unless OMP_NUM_THREADS is set -- a concurrent sweep
# should set it, or 4 runs x 8 threads oversubscribes an 8-core box. Building WITHOUT this
# flag still compiles and runs: every pragma is ignored and the loops run serially.
CXXFLAGS += -fopenmp
LDFLAGS  += -fopenmp

LDLIBS += -lpthread -lrt -lstdc++
LDLIBS += -lstdc++fs

# Profiling
#CXXFLAGS += -pg# flag for profiling with gprof
#CXXFLAGS += -fsanitize=thread# flag for thread sanitizer for debugging

# Parsers
LDLIBS += -llefparseradapt
LDLIBS += -ldefparseradapt
#LDLIBS += -lverilogparser
LDLIBS += -lbookshelfparser -lgzstream -lz

# Boost: HEADER-ONLY here, and deliberately NOT given a -I.
#
# There is no `-I` because there must not be one. This box has TWO Boosts -- 1.71 from apt
# (/usr/include, complete) and a partial 1.80 built into /usr/local -- and gcc searches
# /usr/local/include BEFORE /usr/include, so <boost/...> resolves to 1.80 whatever we write
# here. An -I cannot reorder that (gcc de-duplicates -I against its own system dirs), so a -I
# would only ever misdescribe reality. It did: this line used to read
# `-I${HOME}/local/boost_1_82_0/`, a directory that does not exist, implying Boost 1.82 while
# the build silently used 1.80.
#
# This is safe *because* nothing in this binary links a compiled Boost library: our four Limbo
# archives have zero undefined boost:: symbols and `ldd` shows no libboost. Limbo's own CMake
# does pair 1.80 headers with 1.71 .so for boost_graph/boost_regex -- a real bug, but confined
# to Limbo targets we never build. tools/bootstrap_third_party.sh asserts all of that on every
# run; if the assertion ever fires, read the note it prints before touching anything here.

# Limbo -- a git SUBMODULE (third_party/Limbo, pinned to upstream tag 3.5.2). Nothing about
# Limbo is checked into this repo: HEADERS come from the submodule source tree, LIBS from an
# out-of-tree build (third_party/limbo_install) so the submodule checkout stays pristine.
# A fresh clone must run `tools/bootstrap_third_party.sh` first -- see host/README.md.
LIMBO_DIR     = ${THIRD_PARTY}/Limbo
LIMBO_LIB_DIR = ${THIRD_PARTY}/limbo_install/lib
CPPFLAGS += -I${LIMBO_DIR} -I${TABLE_DIR}/include
CPPFLAGS += -D_GLIBCXX_USE_CXX11_ABI=0   # Limbo's own CMake default (CMAKE_CXX_ABI=0)
LDFLAGS  += -L${LIMBO_LIB_DIR}

# Tabulate
TABLE_DIR = ${THIRD_PARTY}/tabulate

# Enable Vizualization
ifdef BUILD_VIZ
HOST_SRCS += Visualizer.cpp
CPPFLAGS += -DCREATE_VISUALIZATION
LDLIBS += -lcairo
# Cairo is used for generating images, but disabled now because it causes compile errors:
# warning: libnvidia-tls.so.430.50, needed by //usr/lib64/libGL.so.1, not found
#LDLIBS += -l:libcairo.so.2# Specific library for use on nextgenio-amd02 node
endif


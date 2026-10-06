
# sw_only is the CPU-only golden reference by design -- there is no XRT/VCK5000 path here.
# Hardware offload lives in the pl_algo variant (HOST=pl_algo, which honors BUILD_XRT).
# See TODO #9: the two hosts merge once pl_algo bring-up completes.

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = placer/AIEplace.cpp placer/Setup.cpp placer/Schedule.cpp placer/Step.cpp \
	    placer/Partials.cpp placer/Density.cpp placer/Output.cpp placer/BestSolution.cpp \
	    placer/Phase2.cpp placer/MacroLegalize.cpp placer/PositionDump.cpp DCT.cpp
# Parser + data model, shared with pl_algo -- see host/src/common (TODO #9).
COMMON_SRCS = DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
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

# Parsers: native, host/src/common/src/DesignReader.cpp (TODO #43). No Limbo, no Boost, no zlib,
# and so no _GLIBCXX_USE_CXX11_ABI=0 either -- that define existed only to match Limbo's archives.

# Tabulate (header-only, used by Logger) -- the one remaining third-party dependency.
TABLE_DIR = ${THIRD_PARTY}/tabulate
CPPFLAGS += -I${TABLE_DIR}/include


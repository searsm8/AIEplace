
# sw_only is the CPU-only golden reference by design -- there is no XRT/VCK5000 path here.
# Hardware offload lives in the pl_algo variant (HOST=pl_algo, which honors BUILD_XRT).
# See TODO #9: the two hosts merge once pl_algo bring-up completes.
BUILD_VIZ ?= 1# Set to 1 to build with visualization support.

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = placer/AIEplace.cpp placer/Setup.cpp placer/Schedule.cpp placer/Step.cpp \
	    placer/Partials.cpp placer/Density.cpp placer/Output.cpp DataBase.cpp \
	    Grid.cpp Net.cpp DCT.cpp Logger.cpp \
	    Common.cpp
HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# General
CPPFLAGS += -I $(HOST_DIR)/include

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

LDFLAGS += -L$(HOST_DIR)/lib
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

#Boost
CPPFLAGS += -I${HOME}/local/boost_1_82_0/

# Limbo
LIMBO_DIR = ${THIRD_PARTY}/Limbo
CPPFLAGS += -I${LIMBO_DIR} -I${TABLE_DIR}/include
CPPFLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

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


# sw_flat (#45): the one parse and the software placer. Targets grow with the plan's steps. Meow.
#
#   make check-database   FlatDatabase vs sw_only's DataBase and the legacy packer reader on all 44
#                     designs (test/flat_database_check.sh); exits non-zero on any difference. ~2 min.
#   make clean

REPO     := ../..
COMMON   := $(REPO)/host/src/common
BUILD    := build

CXX      ?= g++
CXXFLAGS  = -std=c++2a -O2 -g -fopenmp -MMD -MP
CPPFLAGS  = -I. -I$(REPO)/pl/src -I$(COMMON)/include -I$(REPO)/third_party/tabulate/include
LDFLAGS   = -fopenmp -no-pie
LDLIBS    = -lstdc++fs -ldl

COMMON_SRCS = DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
CHECK_OBJS  = $(addprefix $(BUILD)/, flat_database.o flat_database_check.o $(COMMON_SRCS:.cpp=.o))
vpath %.cpp . test $(COMMON)/src

.PHONY: check-database clean
check-database: $(BUILD)/flat_database_check
	bash test/flat_database_check.sh

$(BUILD)/flat_database_check: $(CHECK_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(wildcard $(BUILD)/*.d)

clean:
	rm -rf $(BUILD)

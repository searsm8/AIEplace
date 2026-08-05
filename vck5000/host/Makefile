
PROJECT_ROOT ?= $(CURDIR)/..

include $(PROJECT_ROOT)/common.mk
include $(HOST_DIR)/makeflags.mk

CXX = g++
CXXFLAGS += -MMD -MP
# Extra preprocessor defines for special builds (e.g. small-grid PL-only field solve:
# EXTRA_DEFS="-DPL_GRID=64 -DPL_FIELD_SOLVE"). Empty by default.
CXXFLAGS += $(EXTRA_DEFS)

ifndef BUILD_XRT
$(info MAKE INFO: Building without XRT - CPU only mode)
endif
ifdef BUILD_XRT
$(info MAKE INFO: Building with XRT support for VCK5000)
endif
ifdef BUILD_VIZ
$(info MAKE INFO: Building with visualization support)
endif

.PHONY: all
all: $(HOST_EXE)

$(HOST_EXE): ${HOST_OBJS}
	$(dir_guard)
	$(info MAKE INFO: Compile AIEplace executable.)
	${CXX} $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Recipes to retrigger .o rebuilds upon changes in depencies (ie. .h files)
-include $(HOST_DEPS)

# Two source roots: the variant's own src/, and the shared parser/data model in
# host/src/common/src (TODO #9). The basenames are disjoint, so make picks the rule whose
# prerequisite .cpp actually exists. The header prerequisites are `wildcard`ed because a
# variant need not have an include/ of its own (pl_algo does not).
$(BUILD_DIR_HOST)/obj/%.o: $(HOST_DIR)/src/%.cpp $(wildcard $(HOST_DIR)/include/*.h) $(wildcard $(HOST_COMMON_DIR)/include/*.h)
	$(dir_guard)
	$(info MAKE INFO: Compile object file "$@")
	${CXX} $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR_HOST)/obj/%.o: $(HOST_COMMON_DIR)/src/%.cpp $(wildcard $(HOST_COMMON_DIR)/include/*.h)
	$(dir_guard)
	$(info MAKE INFO: Compile object file "$@")
	${CXX} $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(HOST_EXE)
	rm -rf $(BUILD_DIR_HOST)/obj

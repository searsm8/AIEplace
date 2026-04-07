
PROJECT_ROOT ?= $(CURDIR)/..

include $(PROJECT_ROOT)/common.mk
include $(HOST_DIR)/makeflags.mk

CXX = g++
CXXFLAGS += -MMD -MP

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

$(BUILD_DIR_HOST)/obj/%.o: $(HOST_DIR)/src/%.cpp $(HOST_DIR)/include/*.h
	$(dir_guard)
	$(info MAKE INFO: Compile object file "$@")
	${CXX} $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(HOST_EXE)
	rm -rf $(BUILD_DIR_HOST)/obj

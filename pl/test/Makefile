# Tier-1 harnesses for pl/: plain g++, no Vitis, no XRT. Each harness computes its own verdict and
# exits 0 (pass) / non-zero (fail).
#
#   make test          build + run every harness in HARNESSES; non-zero exit if any fails
#   make run-<name>    one harness
#   make clean
#
# HARNESSES is the ONLY list: `test` iterates it. vck5000/test's runner kept a second copy and
# silently skipped two harnesses for days (#42). Meow.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
# ../src: the modules and the format contract. host/sw_flat/packer: the encoder the record harnesses
# build their inputs with (tests may include host/; pl/src/ may not). Meow.
INCLUDES  = -I../src -I../../host/sw_flat/packer
BUILD     = build
# -MMD -MP: a header edit rebuilds exactly the harnesses that include it. Meow.
DEPFLAGS  = -MMD -MP

HARNESSES = pin_record_test hpwl_gradient_CU_test
BINS      = $(addprefix $(BUILD)/, $(HARNESSES))

.PHONY: test clean
test: $(BINS)
	@fail=0; \
	for h in $(HARNESSES); do \
		printf '\n=== %s ===\n' "$$h"; \
		$(BUILD)/$$h || fail=1; \
	done; \
	echo; \
	if [ $$fail -eq 0 ]; then echo "PASS: all $(words $(HARNESSES)) pl tier-1 harnesses passed."; \
	else echo "FAIL: at least one pl tier-1 harness failed (see above)."; fi; \
	exit $$fail

run-%: $(BUILD)/%
	$(BUILD)/$*

$(BUILD)/%: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -o $@ $<

clean:
	rm -rf $(BUILD)

-include $(BINS:=.d)

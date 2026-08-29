# Tier-1 offline verification harnesses.
#
# Pure g++ -- no Vitis, no XRT, no device, no xclbin, no toolchain env. Each harness compares
# one module against a golden and exits 0 (pass) / non-zero (fail), so this is a real gate and
# not a report you have to read. The whole suite runs in seconds; run it after every edit under
# ../pl/src/pl_algo/src/modules/.
#
#   make test              build + run everything; non-zero exit if any harness fails
#   make run-<name>        one harness, e.g. make run-fft_pl_test
#   make clean
#
# Everything here currently targets pl_algo -- sw_only has no automated tests yet (TODO #17).
# Tier 2 (HLS C-synthesis, needs Vitis) is synth_check.tcl -- not run from here.
# Tier 3 (sw_emu, needs a built xclbin) is the run-* targets in vck5000/Makefile.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2
# The harnesses #include the real PL modules ("modules/fft_pl.hpp"), not a copy of them --
# that is the point, so a module edit is what the test actually exercises.
INCLUDES  = -I../pl/src/pl_algo/src
BUILD     = build

# sched_verify replays a recorded sw_only schedule against the PL scheduler. The trace and the
# config that produced it are committed side by side -- see fixtures/README.md. Point TRACE at
# another run's trace only if you also update the convergence config in sched_verify.cpp to
# match that run's config_used.json, or the stop check will fail against the wrong threshold.
TRACE ?= fixtures/schedule_trace_adaptec1.csv

HARNESSES = density_model density_bin_model fft_pl_test field_solve_test hpwl_grad_test bb_reduce_test sched_verify
BINS      = $(addprefix $(BUILD)/, $(HARNESSES))

.PHONY: all test clean
all: test

$(BUILD)/%: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

# hpwl_grad_test's assertion [5] is a bounds check, not a value check: dropping either net-mask
# `continue` in hpwl_gradient leaves the gradient BIT-IDENTICAL but indexes bb_DDR[-1] and then
# lut_BRAM[negative], so only a sanitizer can see it. That needs its own target because ASan
# does not run out of the box here -- WSL2's ASLR entropy breaks its shadow mapping and the
# binary spins on AddressSanitizer:DEADLYSIGNAL, so it is run under `setarch -R`. Kept OUT of
# `make test` so the default suite stays fast and needs nothing but g++.
#   make test-asan
ASAN_FLAGS = -std=c++17 -O1 -g -fsanitize=address,undefined

.PHONY: test-asan
test-asan: $(BUILD)/hpwl_grad_test_asan
	@echo "=== hpwl_grad_test (address,undefined) ==="
	@setarch $$(uname -m) -R $(BUILD)/hpwl_grad_test_asan

$(BUILD)/hpwl_grad_test_asan: hpwl_grad_test.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(ASAN_FLAGS) $(INCLUDES) $< -o $@

test: $(BINS)
	@fail=0; \
	for h in density_model density_bin_model fft_pl_test field_solve_test hpwl_grad_test bb_reduce_test; do \
		printf '\n=== %s ===\n' "$$h"; \
		$(BUILD)/$$h || fail=1; \
	done; \
	printf '\n=== sched_verify ===\n'; \
	$(BUILD)/sched_verify $(TRACE) || fail=1; \
	echo; \
	if [ $$fail -eq 0 ]; then echo "PASS: all tier-1 harnesses passed."; \
	else echo "FAIL: at least one tier-1 harness failed (see above)."; fi; \
	exit $$fail

# Single harness: `make run-fft_pl_test`. sched_verify needs its trace argument.
# Do NOT declare the run-% targets .PHONY: GNU make skips implicit/pattern-rule search for phony
# targets, so `.PHONY: $(addprefix run-, $(HARNESSES))` silently disabled the `run-%` rule below
# and every `make run-<name>` except run-sched_verify (which has an explicit rule) printed
# "Nothing to be done" and exited 0. No file named run-* exists, so the rule always fires anyway.
run-sched_verify: $(BUILD)/sched_verify
	$(BUILD)/sched_verify $(TRACE)
run-%: $(BUILD)/%
	$(BUILD)/$*

clean:
	rm -rf $(BUILD)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
LDFLAGS ?=
LDLIBS  ?= -ldl -lm
PYTHON  ?= python3

# ------------------------------------------------------------
# Resolve paths robustly (anchor on repo root)
# This Makefile lives in: <repo>/unittests/Makefile
# ------------------------------------------------------------
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR     := $(abspath $(MAKEFILE_DIR)/..)

SRC_DIR      := $(ROOT_DIR)/src
E_DIR        := $(ROOT_DIR)/e
LIB_DIR      := $(ROOT_DIR)/build/lib
TOOLS_DIR    := $(ROOT_DIR)/unittests
CTESTS_DIR   := $(TOOLS_DIR)/ctests
BUILD_DIR    := $(ROOT_DIR)/build/unittests
OBJ_DIR      := $(BUILD_DIR)

UNIT_RUNNER       := $(BUILD_DIR)/epa_unit_runner
CTEST_RUNNER      := $(BUILD_DIR)/ctest_runner
DYNAMIC_FUZZ_RUNNER := $(BUILD_DIR)/dynamic_fuzz_runner
E_FUZZ_RUNNER     := $(BUILD_DIR)/e_fuzz_runner

# Static E compiler library (built by e/Makefile)
LIBELARA_E   := $(LIB_DIR)/libelara_e.a
# epa_asm_compiler needed in-process for epa_asm_compile_src/epa_asm_compile_file
EPA_ASM_SRC  := $(SRC_DIR)/epa_asm_compiler.c

.PHONY: all unit_runner ctests dynamic-fuzz e-fuzz clean-unit clean-ctests clean print-core print-objs isa-lint

all: isa-lint unit_runner ctests dynamic-fuzz e-fuzz

isa-lint:
	$(PYTHON) $(ROOT_DIR)/scripts/isa_lint.py

# One directory target only (OBJ_DIR == BUILD_DIR)
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# -------------------------
# EPA unit runner
# -------------------------
unit_runner: isa-lint $(LIBELARA_E) $(UNIT_RUNNER)

$(UNIT_RUNNER): $(TOOLS_DIR)/epa_unit_runner.c $(EPA_ASM_SRC) $(LIBELARA_E) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(TOOLS_DIR) -I$(E_DIR) \
	  -o $@ $(TOOLS_DIR)/epa_unit_runner.c $(EPA_ASM_SRC) \
	  -L$(LIB_DIR) -lelara_e $(LDFLAGS) $(LDLIBS)

# -------------------------
# E fuzz runner
# -------------------------
e-fuzz: isa-lint $(LIBELARA_E) $(E_FUZZ_RUNNER)

E_FUZZ_SRCS := \
	$(TOOLS_DIR)/e_fuzz_runner.c \
	$(TOOLS_DIR)/e_fuzz_pump.c \
	$(EPA_ASM_SRC)

$(E_FUZZ_RUNNER): $(E_FUZZ_SRCS) $(LIBELARA_E) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(TOOLS_DIR) -I$(E_DIR) \
	  -o $@ $(E_FUZZ_SRCS) \
	  -L$(LIB_DIR) -lelara_e $(LDFLAGS) $(LDLIBS)

# -------------------------
# C-level unit tests
# -------------------------

# Auto-pick up every test in /unittests/ctests
CTEST_FILES := $(wildcard $(CTESTS_DIR)/*.c)

# Test framework sources (includes registry)
CTEST_TOOL_SRCS := \
	$(TOOLS_DIR)/ctest_main.c \
	$(TOOLS_DIR)/ctest_registry.c

DYNAMIC_FUZZ_SRCS := \
	$(TOOLS_DIR)/dynamic_fuzz_runner.c \
	$(TOOLS_DIR)/dynamic_pool_fuzz_model.c \
	$(SRC_DIR)/memory/epa_dynamic_pool.c

CTEST_SRCS := $(CTEST_TOOL_SRCS) $(CTEST_FILES) $(TOOLS_DIR)/dynamic_pool_fuzz_model.c

# CORE sources (includes at_state + weights/*)
EPA_CORE_NAMES := \
	epa_asm_compiler.c \
	epa_program_loader.c \
	memory/epa_dynamic_pool.c \
	memory/epa_ring_buffer.c \
	memory/epa_stack.c \
	memory/epa_ghs.c \
	atomic_tasks/epa_atomic_tasks.c \
	atomic_tasks/epa_at_router.c \
	atomic_tasks/at_state.c \
	atomic_tasks/at_parallel.c \
	atomic_tasks/at_embed_gather.c \
	atomic_tasks/at_flash_attn.c \
	atomic_tasks/at_kv_cache_append.c \
	atomic_tasks/at_linear_o.c \
	atomic_tasks/at_lm_head_linear.c \
	atomic_tasks/at_mlp_fused.c \
	atomic_tasks/at_residual_add.c \
	atomic_tasks/at_rmsnorm_final.c \
	atomic_tasks/at_rmsnorm_qkv_rope_fused.c \
	atomic_tasks/at_sample_rng.c \
	atomic_tasks/at_topk_topp_filter.c \
	weights/hf_loader.c \
	weights/hf_tensor_index.c \
	weights/hf_shape.c \
	weights/phi3_map.c \
	vm/epa_worker_state.c \
	vm/epa_vm.c \
	threads/epa_thread_pool.c

EPA_CORE_SRCS := $(foreach f,$(EPA_CORE_NAMES),$(wildcard $(SRC_DIR)/$(f)))

ifeq ($(strip $(EPA_CORE_SRCS)),)
$(error No EPA core sources found. Expected some of: $(EPA_CORE_NAMES) under $(SRC_DIR). Fix paths or filenames.)
endif

# ---- Object paths mirror repo layout under build/unittests/ ----
CTEST_OBJS := $(patsubst $(ROOT_DIR)/%.c,$(OBJ_DIR)/%.o,$(CTEST_SRCS))
EPA_OBJS   := $(patsubst $(ROOT_DIR)/%.c,$(OBJ_DIR)/%.o,$(EPA_CORE_SRCS))
DYNAMIC_FUZZ_OBJS := $(patsubst $(ROOT_DIR)/%.c,$(OBJ_DIR)/%.o,$(DYNAMIC_FUZZ_SRCS))

# --- Explicit rules for tool objects (fixes your exact failure) ---
$(OBJ_DIR)/unittests/ctest_main.o: $(TOOLS_DIR)/ctest_main.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(TOOLS_DIR) -c -o $@ $<

$(OBJ_DIR)/unittests/ctest_registry.o: $(TOOLS_DIR)/ctest_registry.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(TOOLS_DIR) -c -o $@ $<

# Generic compile rule for anything else in the repo
$(OBJ_DIR)/%.o: $(ROOT_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(TOOLS_DIR) -c -o $@ $<

ctests: isa-lint $(CTEST_RUNNER)
dynamic-fuzz: isa-lint $(DYNAMIC_FUZZ_RUNNER)

$(CTEST_RUNNER): $(CTEST_OBJS) $(EPA_OBJS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $@ $(CTEST_OBJS) $(EPA_OBJS) $(LDFLAGS) $(LDLIBS)

$(DYNAMIC_FUZZ_RUNNER): $(DYNAMIC_FUZZ_OBJS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $@ $(DYNAMIC_FUZZ_OBJS) $(LDFLAGS) $(LDLIBS)

print-core:
	@echo "EPA_CORE_SRCS:"
	@printf "  %s\n" $(EPA_CORE_SRCS)

print-objs:
	@echo "CTEST_OBJS:"
	@printf "  %s\n" $(CTEST_OBJS)
	@echo "EPA_OBJS:"
	@printf "  %s\n" $(EPA_OBJS)

# -------------------------
# Clean
# -------------------------
clean-unit:
	@rm -f $(UNIT_RUNNER) $(E_FUZZ_RUNNER)

clean-ctests:
	@rm -f $(CTEST_RUNNER) $(DYNAMIC_FUZZ_RUNNER)
	@rm -rf $(OBJ_DIR)/unittests $(OBJ_DIR)/src

clean: clean-unit clean-ctests

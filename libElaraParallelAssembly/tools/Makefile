# ============================================================
# EPA ASM REPL tool (build-only under build/)
# ============================================================

TOOLS_SRC_DIR := tools
TOOLS_BUILD   := build/tools

EPAASM_REPL   := $(TOOLS_BUILD)/epa_asm_repl

CFLAGS += -I../src

EPAASM_REPL_SRCS := \
	$(TOOLS_SRC_DIR)/epa_asm_repl.c \
	src/epa_asm_compiler.c

EPAASM_REPL_OBJS := \
	$(TOOLS_BUILD)/epa_asm_repl.o \
	$(TOOLS_BUILD)/epa_asm_compiler.o
DEPS=$(EPAASM_REPL_OBJS:.o=.d)

.PHONY: tools
tools: $(EPAASM_REPL)

$(EPAASM_REPL): $(EPAASM_REPL_OBJS)
	@$(MKDIR) $(TOOLS_BUILD)
	$(CC) -o $@ $^

$(TOOLS_BUILD)/epa_asm_repl.o: $(TOOLS_SRC_DIR)/epa_asm_repl.c
	@$(MKDIR) $(TOOLS_BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(TOOLS_BUILD)/epa_asm_compiler.o: src/epa_asm_compiler.c
	@$(MKDIR) $(TOOLS_BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

.PHONY: clean-tools
clean-tools:
	$(RM) -r $(TOOLS_BUILD)

-include $(DEPS)

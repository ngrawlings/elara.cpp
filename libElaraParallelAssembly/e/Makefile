CC      ?= gcc
AR      ?= ar
RM      ?= rm -f
MKDIR   ?= mkdir -p

CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
LDFLAGS ?=

TARGET_NAME := ec
OUT_DIR     := $(abspath ../build/e)
LIB_OUT_DIR := $(abspath ../build/lib)
TARGET      := $(OUT_DIR)/$(TARGET_NAME)
EPA_TARGET  := $(OUT_DIR)/e2epa
EPABIN_TARGET := $(OUT_DIR)/e2epabin
STATIC_LIB  := $(LIB_OUT_DIR)/libelara_e.a
EPA_BUILD_DIR := $(abspath ../build/epaasm_examples)

# Library sources: all E pipeline sources (no main() files)
LIB_SRCS := \
	e_compiler.c \
	e_preprocess.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c \
	e_emit_epa.c

SRCS := \
	ec.c \
	e_preprocess.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c

EPA_SRCS := \
	e2epa.c \
	e_preprocess.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c \
	e_emit_epa.c \
	../src/epa_asm_compiler.c

EPABIN_SRCS := \
	e2epabin.c \
	e_preprocess.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c \
	e_emit_epa.c \
	../src/epa_asm_compiler.c

LIB_OBJS    := $(LIB_SRCS:%.c=$(OUT_DIR)/%.lib.o)
OBJS        := $(SRCS:%.c=$(OUT_DIR)/%.o)
EPA_OBJS    := $(EPA_SRCS:%.c=$(OUT_DIR)/%.epa.o)
EPABIN_OBJS := $(EPABIN_SRCS:%.c=$(OUT_DIR)/%.epabin.o)
EXAMPLES := $(wildcard examples/*.e)
EPA_OUTPUTS := $(EXAMPLES:examples/%.e=$(EPA_BUILD_DIR)/%.epaasm)

.PHONY: all lib clean epa-build

all: $(TARGET) $(EPA_TARGET) $(EPABIN_TARGET) $(STATIC_LIB)

$(TARGET): $(OBJS)
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(EPA_TARGET): $(EPA_OBJS)
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(EPA_OBJS) -o $@ $(LDFLAGS)

$(EPABIN_TARGET): $(EPABIN_OBJS)
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(EPABIN_OBJS) -o $@ $(LDFLAGS)

$(STATIC_LIB): $(LIB_OBJS)
	@$(MKDIR) $(LIB_OUT_DIR)
	$(AR) rcs $@ $^

lib: $(STATIC_LIB)

$(OUT_DIR)/%.lib.o: %.c
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: %.c
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.epa.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.epabin.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(EPA_BUILD_DIR)/%.epaasm: examples/%.e $(EPA_TARGET)
	@$(MKDIR) $(EPA_BUILD_DIR)
	$(EPA_TARGET) $< $@

epa-build: $(EPA_OUTPUTS)

clean:
	$(RM) $(OBJS)
	$(RM) $(EPA_OBJS)
	$(RM) $(LIB_OBJS)
	$(RM) $(STATIC_LIB)
	$(RM) -r $(OUT_DIR)

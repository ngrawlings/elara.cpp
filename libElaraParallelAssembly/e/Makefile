CC      ?= gcc
RM      ?= rm -f
MKDIR   ?= mkdir -p

CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=

TARGET_NAME := ec
OUT_DIR     := ../build/e
TARGET      := $(OUT_DIR)/$(TARGET_NAME)
EPA_TARGET  := $(OUT_DIR)/e2epa
EPA_BUILD_DIR := ../epa_build

SRCS := \
	ec.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c

EPA_SRCS := \
	e2epa.c \
	e_lexer.c \
	e_parser.c \
	e_ast.c \
	e_semantic.c \
	e_templates.c \
	e_emit_epa.c

OBJS := $(SRCS:%.c=$(OUT_DIR)/%.o)
EPA_OBJS := $(EPA_SRCS:%.c=$(OUT_DIR)/%.epa.o)
EXAMPLES := $(wildcard examples/*.e)
EPA_OUTPUTS := $(EXAMPLES:examples/%.e=$(EPA_BUILD_DIR)/%.epaasm)

.PHONY: all clean epa-build

all: $(TARGET) $(EPA_TARGET)

$(TARGET): $(OBJS)
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(EPA_TARGET): $(EPA_OBJS)
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(EPA_OBJS) -o $@ $(LDFLAGS)

$(OUT_DIR)/%.o: %.c
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.epa.o: %.c
	@$(MKDIR) $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(EPA_BUILD_DIR)/%.epaasm: examples/%.e $(EPA_TARGET)
	@$(MKDIR) $(EPA_BUILD_DIR)
	$(EPA_TARGET) $< $@

epa-build: $(EPA_OUTPUTS)

clean:
	$(RM) $(OBJS)
	$(RM) $(EPA_OBJS)
	$(RM) -r $(OUT_DIR)

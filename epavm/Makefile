NAME=epavm
CXX=g++
CC?=gcc

FRAMEWORK_ROOT?=$(abspath ..)
ELARA_ROOT?=$(if $(wildcard $(FRAMEWORK_ROOT)/build/include),$(FRAMEWORK_ROOT)/build,$(abspath ../build))
ELARA_INCLUDE_DIR?=$(ELARA_ROOT)/include
ELARA_LIB_DIR?=$(ELARA_ROOT)/lib
STAGE_BIN_DIR?=$(ELARA_ROOT)/bin
EPA_PARALLEL_SRC_DIR?=$(FRAMEWORK_ROOT)/libElaraParallelAssembly/src
EPA_PARALLEL_OPCODES_DIR?=$(FRAMEWORK_ROOT)/libElaraParallelAssembly/src/opcodes

BUILD_PROFILE?=release
COMMON_CFLAGS=-std=gnu++11
DEBUG_CFLAGS=$(COMMON_CFLAGS) -O0 -g3
RELEASE_CFLAGS=$(COMMON_CFLAGS) -O3 -DNDEBUG
ifeq ($(BUILD_PROFILE),debug)
PROFILE_CFLAGS=$(DEBUG_CFLAGS)
else
PROFILE_CFLAGS=$(RELEASE_CFLAGS)
endif
STD_CCFLAGS=-std=gnu11 $(filter-out -std=gnu++11,$(PROFILE_CFLAGS))
PROJECT_CFLAGS=$(filter-out -std=gnu++11,$(CFLAGS))
STD_CFLAGS=-c -MMD -MP $(PROFILE_CFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) -I$(EPA_PARALLEL_SRC_DIR) -I$(EPA_PARALLEL_OPCODES_DIR)
STD_LDFLAGS=-L$(ELARA_LIB_DIR) \
    -lelarasockets -lelarathreads -lelaraevent \
    -lelaraformat -lelaraio -lelaradebug -lelaracore \
    -lelaraparallelassembly \
    -ldl -lrt -lm -levent -levent_pthreads -pthread

BUILDPATH=./build
TARGET=$(NAME)

CPP_SOURCES=$(shell find ./src -type f -name '*.cpp' -print)
C_SOURCES=$(shell find ./src -type f -name '*.c' -print)
CPP_OBJECTS=$(patsubst ./src/%.cpp,$(BUILDPATH)/src/%.o,$(CPP_SOURCES))
C_OBJECTS=$(patsubst ./src/%.c,$(BUILDPATH)/src/%.c.o,$(C_SOURCES))
OBJECTS=$(CPP_OBJECTS) $(C_OBJECTS)
DEPS=$(CPP_OBJECTS:.o=.d) $(C_OBJECTS:.o=.d)

.PHONY: all install remove clean cleanconf

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)

$(BUILDPATH)/src/%.o: ./src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD_CFLAGS) $(CFLAGS) $< -o $@

$(BUILDPATH)/src/%.c.o: ./src/%.c
	@mkdir -p $(dir $@)
	$(CC) -c -MMD -MP $(STD_CCFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) -I$(EPA_PARALLEL_SRC_DIR) -I$(EPA_PARALLEL_OPCODES_DIR) $(PROJECT_CFLAGS) $< -o $@

-include $(wildcard $(DEPS))

install: all
	mkdir -p $(STAGE_BIN_DIR)
	cp $(BUILDPATH)/$(TARGET) $(STAGE_BIN_DIR)/$(TARGET)

remove:
	rm -f $(STAGE_BIN_DIR)/$(TARGET)

clean:
	rm -rf $(BUILDPATH)

cleanconf:
	rm -f config.log config.status Makefile configure

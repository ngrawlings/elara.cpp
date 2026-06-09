NAME=elara-os
CXX=g++
CC?=gcc
DEFS =
FRAMEWORK_ROOT?=$(abspath ../../..)
ELARA_ROOT?=$(if $(wildcard $(FRAMEWORK_ROOT)/build/include),$(FRAMEWORK_ROOT)/build,$(abspath ../build))
ELARA_INCLUDE_DIR?=$(ELARA_ROOT)/include
ELARA_LIB_DIR?=$(ELARA_ROOT)/lib
EPA_PARALLEL_SRC_DIR?=$(FRAMEWORK_ROOT)/libElaraParallelAssembly/src
EPA_PARALLEL_OPCODES_DIR?=$(FRAMEWORK_ROOT)/libElaraParallelAssembly/src/opcodes
PREFIX?=$(abspath ./dist)
BIN_DIR?=$(PREFIX)/bin
SHARE_DIR?=$(PREFIX)/share/elara-os
INSTALL_MANIFEST?=$(SHARE_DIR)/install-manifest.txt
BUILDPATH=./build
COMMON_CFLAGS=-std=gnu++11
DEBUG_CFLAGS=$(COMMON_CFLAGS) -O0 -g3
RELEASE_CFLAGS=$(COMMON_CFLAGS) -O3 -DNDEBUG
BUILD_PROFILE?=release
ifeq ($(BUILD_PROFILE),debug)
PROFILE_CFLAGS=$(DEBUG_CFLAGS)
else
PROFILE_CFLAGS=$(RELEASE_CFLAGS)
endif
STD_CCFLAGS=-std=gnu11 $(filter-out -std=gnu++11,$(PROFILE_CFLAGS))
STD_CFLAGS=-c $(PROFILE_CFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) -I$(EPA_PARALLEL_SRC_DIR) -I$(EPA_PARALLEL_OPCODES_DIR) $(DEFS)
STD_LDFLAGS=-L$(ELARA_LIB_DIR) -lelaraui -lelarauirpc -lelarasockets -lelarathreads -lelaraevent -lelaravectorcpp -lelaravector -lelaraformat -lelaraio -lelaradebug -lelaracore -lelaraparallelassembly -ldl -lrt -levent -levent_pthreads -pthread
TARGET=elara-os
DEBUG_TARGET=$(TARGET)-epa-debug
CPP_SOURCES=$(shell find ./src -type f -name '*.cpp' -print)
C_SOURCES=$(shell find ./src -type f -name '*.c' -print)
APP_CPP_SOURCES=$(filter-out ./src/epa_debug_main.cpp,$(CPP_SOURCES))
DEBUG_CPP_SOURCES=$(filter-out ./src/main.cpp,$(CPP_SOURCES))
APP_CPP_OBJECTS=$(patsubst ./src/%.cpp,build/src/%.o,$(APP_CPP_SOURCES))
DEBUG_CPP_OBJECTS=$(patsubst ./src/%.cpp,build/src/%.debug.o,$(DEBUG_CPP_SOURCES))
C_OBJECTS=$(patsubst ./src/%.c,build/src/%.c.o,$(C_SOURCES))
OBJECTS=$(APP_CPP_OBJECTS) $(C_OBJECTS)

.PHONY: all install clean remove cleanconf

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)

$(DEBUG_TARGET): $(DEBUG_CPP_OBJECTS) $(C_OBJECTS)
	$(CXX) $(DEBUG_CPP_OBJECTS) $(C_OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(DEBUG_TARGET)

all: $(DEBUG_TARGET)

build/src/%.o: ./src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@

build/src/%.debug.o: ./src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@

build/src/%.c.o: ./src/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(STD_CCFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) -I$(EPA_PARALLEL_SRC_DIR) -I$(EPA_PARALLEL_OPCODES_DIR) $(DEFS) $(CFLAGS) ./src/$*.c -o $@

install:
	@if [ ! -x "$(BUILDPATH)/$(TARGET)" ]; then \
		echo "Missing built binary $(BUILDPATH)/$(TARGET). Run ./build.sh first."; \
		exit 1; \
	fi
	mkdir -p $(BIN_DIR)
	cp $(BUILDPATH)/$(TARGET) $(BIN_DIR)/$(TARGET)
	mkdir -p $(SHARE_DIR)
	printf '%s\n' "$(BIN_DIR)/$(TARGET)" > $(INSTALL_MANIFEST)

clean:
	rm -rf $(BUILDPATH)

remove:
	@if [ -f "$(INSTALL_MANIFEST)" ]; then \
		while IFS= read -r installed_path; do \
			rm -f "$$installed_path"; \
		done < "$(INSTALL_MANIFEST)"; \
		rm -f "$(INSTALL_MANIFEST)"; \
	else \
		rm -f $(BIN_DIR)/$(TARGET); \
	fi
	@rmdir $(SHARE_DIR) 2>/dev/null || true

cleanconf:
	rm -f config.log
	rm -f config.status
	rm -f Makefile
	rm -f configure

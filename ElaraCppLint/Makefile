NAME=elaracpplint
CC=g++
DEFS =
ROOT_BUILD_DIR?=$(abspath ../build)
STAGE_INCLUDE_DIR=$(ROOT_BUILD_DIR)/include
STAGE_LIB_DIR=$(ROOT_BUILD_DIR)/lib
STAGE_BIN_DIR=$(ROOT_BUILD_DIR)/bin
STD_CFLAGS=-c -I$(shell pwd) -I$(STAGE_INCLUDE_DIR) $(DEFS)
STD_LDFLAGS=-L$(STAGE_LIB_DIR) -lelaraio -lelaracore
SOURCES=$(shell find ./src -type f -name '*.cpp' -print)
OBJECTS=$(patsubst ./src/%.cpp,build/src/%.o,$(SOURCES))
BUILDPATH=./build
TARGET=elara.cpp-lint

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)

build/src/%.o:
	@mkdir -p $(dir $@)
	$(CC) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@

install:
	mkdir -p $(STAGE_BIN_DIR)
	cp $(BUILDPATH)/$(TARGET) $(STAGE_BIN_DIR)/$(TARGET)

clean:
	rm -rf $(BUILDPATH)

remove:
	rm -f $(STAGE_BIN_DIR)/$(TARGET)

cleanconf:
	rm -f config.log
	rm -f config.status
	rm -f Makefile
	rm -f configure

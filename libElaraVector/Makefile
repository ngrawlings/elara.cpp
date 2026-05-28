C_NAME=libelaravector
CPP_NAME=libelaravectorcpp
CC=gcc
CXX=g++
AR=ar
DEFS = -DHAVE_CONFIG_H
ROOT_BUILD_DIR?=$(abspath ../build)
STAGE_INCLUDE_DIR=$(ROOT_BUILD_DIR)/include
STAGE_LIB_DIR=$(ROOT_BUILD_DIR)/lib
STD_CFLAGS= -c  -MMD -MP -I$(shell pwd) -I$(STAGE_INCLUDE_DIR) $(DEFS)
C_CFLAGS=$(filter-out -std=gnu++11,$(CFLAGS))
CPP_CFLAGS=$(CFLAGS)
BUILDPATH=./build

C_SOURCES=$(shell find ./$(C_NAME) \( -name x_*  \) -prune -o -type f -name '*.c' -print)
C_HEADERS=$(shell find ./$(C_NAME) \( -name x_*  \) -prune -o -type f -name '*.h' -print)
C_OBJECTS=$(patsubst ./$(C_NAME)/%.c,$(BUILDPATH)/c/%.o,$(C_SOURCES))

CPP_SOURCES=$(shell find ./$(CPP_NAME) \( -name x_*  \) -prune -o -type f -name '*.cpp' -print)
CPP_HEADERS=$(shell find ./$(CPP_NAME) \( -name x_*  \) -prune -o -type f -name '*.h' -print)
CPP_OBJECTS=$(patsubst ./$(CPP_NAME)/%.cpp,$(BUILDPATH)/cpp/%.o,$(CPP_SOURCES))
DEPS=$(C_OBJECTS:.o=.d) $(CPP_OBJECTS:.o=.d)

C_LIBRARY=$(C_NAME).a
CPP_LIBRARY=$(CPP_NAME).a
C_INSTALL_HEADER_PATH=$(STAGE_INCLUDE_DIR)/$(C_NAME)
CPP_INSTALL_HEADER_PATH=$(STAGE_INCLUDE_DIR)/$(CPP_NAME)

all: $(C_LIBRARY) $(CPP_LIBRARY)

$(C_LIBRARY): $(C_OBJECTS)
	$(AR) rcs $(BUILDPATH)/$@ $(C_OBJECTS)

$(CPP_LIBRARY): $(CPP_OBJECTS)
	$(AR) rcs $(BUILDPATH)/$@ $(CPP_OBJECTS)

$(BUILDPATH)/c/%.o: ./$(C_NAME)/%.c copyconfig
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(CC) $(STD_CFLAGS) $(C_CFLAGS) $< -o $@

$(BUILDPATH)/cpp/%.o: ./$(CPP_NAME)/%.cpp
	@[ -d $(@D) ] || mkdir -p $(@D)
	$(CXX) $(STD_CFLAGS) $(CPP_CFLAGS) $< -o $@

copyconfig:
	cp config.h ./$(C_NAME)/config.h

install:
	@for header in $(C_HEADERS); do \
		rel="$${header#./$(C_NAME)/}"; \
		dst="$(C_INSTALL_HEADER_PATH)/$$rel"; \
		[ -d "$$(dirname "$$dst")" ] || mkdir -p "$$(dirname "$$dst")"; \
		cp "$$header" "$$dst" || exit $$?; \
	done
	@for header in $(CPP_HEADERS); do \
		rel="$${header#./$(CPP_NAME)/}"; \
		dst="$(CPP_INSTALL_HEADER_PATH)/$$rel"; \
		[ -d "$$(dirname "$$dst")" ] || mkdir -p "$$(dirname "$$dst")"; \
		cp "$$header" "$$dst" || exit $$?; \
	done
	mkdir -p $(STAGE_LIB_DIR)
	cp $(BUILDPATH)/$(C_LIBRARY) $(STAGE_LIB_DIR)/$(C_LIBRARY)
	cp $(BUILDPATH)/$(CPP_LIBRARY) $(STAGE_LIB_DIR)/$(CPP_LIBRARY)

clean:
	rm -Rf $(BUILDPATH)/c $(BUILDPATH)/cpp
	rm -f $(BUILDPATH)/$(C_LIBRARY) $(BUILDPATH)/$(CPP_LIBRARY)

remove:
	rm -Rf $(C_INSTALL_HEADER_PATH) $(CPP_INSTALL_HEADER_PATH)
	rm -f $(STAGE_LIB_DIR)/$(C_LIBRARY) $(STAGE_LIB_DIR)/$(CPP_LIBRARY)

cleanconf:
	rm -Rf config.status
	rm -f config.log
	rm -f Makefile

-include $(wildcard $(DEPS))

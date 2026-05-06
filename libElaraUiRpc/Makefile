NAME=libelarauirpc
CC=g++
AR=ar
DEFS=-DHAVE_CONFIG_H
ROOT_BUILD_DIR?=$(abspath ../build)
STAGE_INCLUDE_DIR=$(ROOT_BUILD_DIR)/include
STAGE_LIB_DIR=$(ROOT_BUILD_DIR)/lib
BUILDPATH=./build
INSTALL_HEADER_PATH=$(STAGE_INCLUDE_DIR)/$(NAME)
INSTALL_LIB_PATH=$(STAGE_LIB_DIR)
STATIC_LIBRARY=$(NAME).a

STD_CFLAGS= -c -I$(shell pwd) -I../libElaraUI -I$(STAGE_INCLUDE_DIR) $(DEFS)
STD_LDFLAGS=

SOURCES=$(shell for file in `find ./$(NAME) \( -name x_* \) -prune -o -type f -name '*.cpp' -print`; do echo $$file; done)
HEADERS=$(shell for file in `find ./$(NAME) \( -name x_* \) -prune -o -type f -name '*.h' -print`; do echo $$file; done)
OBJECTS=$(subst $(NAME),build,$(SOURCES:.cpp=.o))

all: $(SOURCES) $(STATIC_LIBRARY)

$(STATIC_LIBRARY): $(OBJECTS)
	@[ -d $(BUILDPATH) ] || mkdir -p $(BUILDPATH)
	$(AR) rcs $(BUILDPATH)/$@ $(addprefix $(BUILDPATH)/, $(notdir $(OBJECTS)))

$(BUILDPATH)/%.o: copyconfig
	@[ -d $(BUILDPATH) ] || mkdir -p $(BUILDPATH)
	$(CC) $(STD_CFLAGS) $(CFLAGS) ./$(subst build,$(NAME),$(@:.o=.cpp)) -o $(BUILDPATH)/$(notdir $@)

copyconfig:
	@[ -d ./$(NAME) ] || mkdir -p ./$(NAME)
	cp config.h ./$(NAME)/config.h

install: all
	@for header in $(HEADERS); do \
		rel="$${header#./$(NAME)/}"; \
		dst="$(INSTALL_HEADER_PATH)/$$rel"; \
		[ -d "$$(dirname "$$dst")" ] || mkdir -p "$$(dirname "$$dst")"; \
		cp "$$header" "$$dst" || exit $$?; \
	done
	mkdir -p $(INSTALL_LIB_PATH)
	cp $(BUILDPATH)/$(STATIC_LIBRARY) $(INSTALL_LIB_PATH)/$(STATIC_LIBRARY)

clean:
	rm -Rf $(BUILDPATH)/*.o
	rm -f $(BUILDPATH)/$(STATIC_LIBRARY)

remove:
	rm -Rf $(INSTALL_HEADER_PATH)
	rm -f $(INSTALL_LIB_PATH)/$(STATIC_LIBRARY)

cleanconf:
	rm -Rf config.status autom4te.cache
	rm -f config.log config.h Makefile

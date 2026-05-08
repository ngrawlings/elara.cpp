NAME=libelaraui
CC=g++
DEFS=-DHAVE_CONFIG_H
ROOT_BUILD_DIR?=$(abspath ../build)
STAGE_INCLUDE_DIR=$(ROOT_BUILD_DIR)/include
STAGE_LIB_DIR=$(ROOT_BUILD_DIR)/lib
BUILDPATH=./build
INSTALL_HEADER_PATH=$(STAGE_INCLUDE_DIR)/$(NAME)
INSTALL_LIB_PATH=$(STAGE_LIB_DIR)
STATIC_LIBRARY=$(NAME).a

GTK_CFLAGS=-I/usr/include/gtk-4.0 -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -I/usr/include/sysprof-6 -I/usr/include/harfbuzz -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/libmount -I/usr/include/blkid -I/usr/include/fribidi -I/usr/include/cairo -I/usr/include/pixman-1 -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/aarch64-linux-gnu -I/usr/include/webp -I/usr/include/graphene-1.0 -I/usr/lib/aarch64-linux-gnu/graphene-1.0/include -pthread 
GTK_LIBS=-lgtk-4 -lpangocairo-1.0 -lpango-1.0 -lharfbuzz -lgdk_pixbuf-2.0 -lcairo-gobject -lcairo -lvulkan -lgraphene-1.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 
GTK_BACKEND_ENABLED=1

STD_CFLAGS= -c  -I./libelaraui -I$(shell pwd) -I$(STAGE_INCLUDE_DIR) $(GTK_CFLAGS) $(DEFS)
STD_LDFLAGS=$(GTK_LIBS)

SOURCES=$(shell for file in `find ./$(NAME) \( -name x_*  \) -prune -o -type f -name '*.cpp' -print`; do echo $$file; done)
HEADERS=$(shell for file in `find ./$(NAME) \( -name x_* \) -prune -o -type f -name '*.h' -print`; do echo $$file; done)
OBJECTS=$(subst $(NAME),build,$(SOURCES:.cpp=.o))
DST_HEADERS=$(subst ./$(NAME),$(INSTALL_HEADER_PATH),$(HEADERS))

DEMO_SOURCE=./demo/main.cpp
DEMO_TARGET=$(BUILDPATH)/bin/libelaraui-demo
DEMO_CFLAGS=  -I../ -I../libelaraui -I$(shell pwd) -I$(STAGE_INCLUDE_DIR) $(GTK_CFLAGS) $(DEFS)
DEMO_LDFLAGS=-L$(BUILDPATH) -lelaraui -L$(STAGE_LIB_DIR) -lelaravectorcpp -lelaravector -lelaraformat -lelaraio -lelaracore $(GTK_LIBS)
RPC_SERVER_SOURCE=./demo/rpc_server.cpp
RPC_SERVER_TARGET=$(BUILDPATH)/bin/elaraui-server
RPC_SERVER_STAGE_TARGET=$(ROOT_BUILD_DIR)/bin/elaraui-server
RPC_SERVER_CFLAGS=  -I../ -I../libElaraUiRpc -I../libelaraui -I$(shell pwd) -I$(STAGE_INCLUDE_DIR) $(GTK_CFLAGS) $(DEFS)
RPC_SERVER_LDFLAGS=-L$(STAGE_LIB_DIR) -lelaraui -lelarauirpc -lelarasockets -lelarathreads -lelaraevent -lelaravectorcpp -lelaravector -lelaraformat -lelaraio -lelaracore $(GTK_LIBS)

all: $(SOURCES) $(STATIC_LIBRARY)

$(STATIC_LIBRARY): $(OBJECTS)
	@[ -d $(BUILDPATH) ] || mkdir -p $(BUILDPATH)
	ar rcs $(BUILDPATH)/$@ $(addprefix $(BUILDPATH)/, $(notdir $(OBJECTS)))

$(BUILDPATH)/%.o: copyconfig
	@[ -d $(BUILDPATH) ] || mkdir -p $(BUILDPATH)
	$(CC) $(STD_CFLAGS) $(CFLAGS) ./$(subst build,$(NAME),$(@:.o=.cpp)) -o $(BUILDPATH)/$(notdir $@)

demo: $(STATIC_LIBRARY)
	@if [ "$(GTK_BACKEND_ENABLED)" != "1" ]; then \
		echo "GTK backend is disabled. Reconfigure without --disable-gtk to build the demo."; \
		exit 1; \
	fi
	@[ -d $(BUILDPATH)/bin ] || mkdir -p $(BUILDPATH)/bin
	$(CC) $(DEMO_CFLAGS) $(CFLAGS) $(DEMO_SOURCE) -o $(DEMO_TARGET) $(DEMO_LDFLAGS)

run-demo: demo
	$(DEMO_TARGET)

rpc-server: $(STATIC_LIBRARY)
	@if [ "$(GTK_BACKEND_ENABLED)" != "1" ]; then \
		echo "GTK backend is disabled. Reconfigure without --disable-gtk to build elaraui-server."; \
		exit 1; \
	fi
	@[ -d $(BUILDPATH)/bin ] || mkdir -p $(BUILDPATH)/bin
	@[ -d $(ROOT_BUILD_DIR)/bin ] || mkdir -p $(ROOT_BUILD_DIR)/bin
	$(CC) $(RPC_SERVER_CFLAGS) $(CFLAGS) $(RPC_SERVER_SOURCE) -o $(RPC_SERVER_TARGET) $(RPC_SERVER_LDFLAGS)
	cp $(RPC_SERVER_TARGET) $(RPC_SERVER_STAGE_TARGET)

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
	@if [ "$(GTK_BACKEND_ENABLED)" = "1" ]; then \
		$(MAKE) rpc-server ROOT_BUILD_DIR="$(ROOT_BUILD_DIR)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" || exit $$?; \
	fi

clean:
	rm -Rf $(BUILDPATH)/*.o
	rm -f $(BUILDPATH)/$(STATIC_LIBRARY)
	rm -f $(DEMO_TARGET)
	rm -f $(RPC_SERVER_TARGET)
	rm -f $(RPC_SERVER_STAGE_TARGET)

remove:
	rm -Rf $(INSTALL_HEADER_PATH)
	rm -f $(INSTALL_LIB_PATH)/$(STATIC_LIBRARY)
	rm -f $(RPC_SERVER_STAGE_TARGET)

cleanconf:
	rm -Rf config.status autom4te.cache
	rm -f config.log config.h Makefile

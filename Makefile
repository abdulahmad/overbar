# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 abdulahmad and contributors

CC ?= gcc
AR ?= ar
# Warnings are reported, not fatal, so a newer compiler that warns about something new still
# builds. CI makes them errors (make test WERROR=-Werror, make check), so Overbar itself stays
# warning-free under every port's flags.
WERROR ?=
CFLAGS ?= -std=c99 -Wall -Wextra $(WERROR) -Iinclude

LIB_OBJS = \
	src/scaler.o \
	src/host.o \
	src/present.o \
	src/menu.o \
	src/look.o \
	src/overlay.o \
	src/filemenu.o \
	src/net.o \
	src/snap.o \
	src/rollback.o \
	src/netgame.o \
	src/plat.o \
	src/plat_pads.o \
	third_party/pulseman/menu.o
ifdef SDL
CFLAGS += -DOVERBAR_SDL $(shell pkg-config --cflags sdl2)
LIB_OBJS += src/present_sdl.o
endif

.PHONY: all test check clean shots test-plat

# Winsock, for online play (src/net.c)
ifeq ($(OS),Windows_NT)
NETLIBS ?= -lws2_32
endif

all: liboverbar.a tests/test_overbar tests/test_net tests/test_snap

liboverbar.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

tests/test_overbar: tests/test_overbar.c liboverbar.a
	$(CC) $(CFLAGS) -o $@ tests/test_overbar.c liboverbar.a -lm

tests/test_net: tests/test_net.c liboverbar.a
	$(CC) $(CFLAGS) -o $@ tests/test_net.c liboverbar.a -lm $(NETLIBS)

tests/test_snap: tests/test_snap.c liboverbar.a
	$(CC) $(CFLAGS) -o $@ tests/test_snap.c liboverbar.a -lm

test: tests/test_overbar tests/test_net tests/test_snap
	./tests/test_overbar
	./tests/test_net
	./tests/test_snap

# the platform layer on SDL's dummy drivers (needs SDL2): make test-plat SDL=1
test-plat: tests/test_plat
	./tests/test_plat

tests/test_plat: tests/test_plat.c liboverbar.a
	$(CC) $(CFLAGS) -DOB_SDL_BASE -o $@ tests/test_plat.c liboverbar.a -lm $(NETLIBS) $(shell pkg-config --libs sdl2)

# every source with the strictest port's flags (NHL94Genesis-C-Port's) in the SDL client build,
# warnings as errors. CC=i686-w64-mingw32-gcc (and so on) checks a cross compiler; tests/check.sh
# runs them all.
STRICT ?= -std=gnu11 -O2 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
	-Wno-unused-parameter -Werror
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null)
check:
	@for f in $$(cat sources.txt); do \
	    echo "$(CC) $$f"; \
	    $(CC) $(STRICT) -Iinclude -DOB_SDL_BASE $(SDL_CFLAGS) -c $$f -o check.o || exit 1; \
	done; rm -f check.o

clean:
	rm -f $(LIB_OBJS) src/present_sdl.o liboverbar.a tests/test_overbar tests/test_overbar.exe tests/test_net tests/test_net.exe tests/test_snap tests/test_snap.exe check.o tools/shots

src/%.o: src/%.c include/overbar/overbar.h include/overbar/net.h src/look_tables.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/net.o: third_party/pulseman/udp.c

third_party/pulseman/menu.o: third_party/pulseman/menu.c include/mdrt/menu.h
	$(CC) $(CFLAGS) -c -o $@ $<

# the README's pictures (docs/images) from the samples in docs/samples, by tools/shots.c; needs
# Python with Pillow to read the samples. Run from a folder with mister/ beside it for the MiSTer one.
shots: liboverbar.a tools/shots.c
	mkdir -p docs/images build/shots
	for s in genesis snes dos; do \
	    python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" \
	        docs/samples/$$s.png build/shots/$$s.ppm || exit 1; \
	done
	$(CC) $(CFLAGS) -o tools/shots tools/shots.c liboverbar.a -lm
	./tools/shots docs/images build/shots/genesis.ppm build/shots/snes.ppm build/shots/dos.ppm

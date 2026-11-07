# SPDX-License-Identifier: MIT
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
	src/overlay.o \
	src/filemenu.o \
	third_party/pulseman/menu.o
ifdef SDL
CFLAGS += -DOVERBAR_SDL $(shell pkg-config --cflags sdl2)
LIB_OBJS += src/present_sdl.o
endif

.PHONY: all test check clean

all: liboverbar.a tests/test_overbar

liboverbar.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

tests/test_overbar: tests/test_overbar.c liboverbar.a
	$(CC) $(CFLAGS) -o $@ tests/test_overbar.c liboverbar.a

test: tests/test_overbar
	./tests/test_overbar

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
	rm -f $(LIB_OBJS) src/present_sdl.o liboverbar.a tests/test_overbar tests/test_overbar.exe check.o

src/%.o: src/%.c include/overbar/overbar.h
	$(CC) $(CFLAGS) -c -o $@ $<

third_party/pulseman/menu.o: third_party/pulseman/menu.c include/mdrt/menu.h
	$(CC) $(CFLAGS) -c -o $@ $<

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 abdulahmad and contributors

CC ?= gcc
AR ?= ar
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -Iinclude

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

.PHONY: all test clean

all: liboverbar.a tests/test_overbar

liboverbar.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

tests/test_overbar: tests/test_overbar.c liboverbar.a
	$(CC) $(CFLAGS) -o $@ tests/test_overbar.c liboverbar.a

test: tests/test_overbar
	./tests/test_overbar

clean:
	rm -f $(LIB_OBJS) liboverbar.a tests/test_overbar

src/%.o: src/%.c include/overbar/overbar.h
	$(CC) $(CFLAGS) -c -o $@ $<

third_party/pulseman/menu.o: third_party/pulseman/menu.c include/mdrt/menu.h
	$(CC) $(CFLAGS) -c -o $@ $<

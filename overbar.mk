# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 abdulahmad
#
# Overbar's sources, for a port that has Overbar as a git submodule and compiles it itself:
#
#   include native/overbar/overbar.mk
#   ... compile $(OVERBAR_SRCS) and $(OVERBAR_PULSEMAN_SRC) with the port's own compiler and
#   flags, plus $(OVERBAR_INC) $(OVERBAR_DEFS)
#
# third_party/pulseman/menu.c has the same file name as src/menu.c, so give its object another
# name (pulseman_menu.o). Shell build scripts read sources.txt, which lists the same files.
# src/net.c (online play) includes third_party/pulseman/udp.c itself, and on Windows needs
# -lws2_32 at the link.
#
# The MiSTer filter files (mister/, GPL-3.0 data, see mister/README.md) are read at run time from a
# mister folder next to the program. $(call overbar_copy_mister,DIR) copies them to DIR/mister.

OVERBAR_DIR          ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
OVERBAR_LIB_NAMES    := scaler host present menu look overlay filemenu bar_win net plat plat_pads osk gl sides chat room
# macOS: the Cocoa menu bar is Objective-C: compile $(OVERBAR_MAC_SRCS) with -fobjc-arc and link $(OVERBAR_MAC_LIBS)
ifeq ($(shell uname -s),Darwin)
OVERBAR_MAC_SRCS     := $(OVERBAR_DIR)/src/bar_mac.m
OVERBAR_MAC_LIBS     := -framework Cocoa
endif
OVERBAR_SRCS         := $(OVERBAR_LIB_NAMES:%=$(OVERBAR_DIR)/src/%.c)
OVERBAR_PULSEMAN_SRC := $(OVERBAR_DIR)/third_party/pulseman/menu.c
OVERBAR_HDRS         := $(OVERBAR_DIR)/include/overbar/overbar.h $(OVERBAR_DIR)/include/overbar/net.h $(OVERBAR_DIR)/src/look_tables.h $(OVERBAR_DIR)/third_party/pulseman/udp.c $(OVERBAR_DIR)/include/mdrt/menu.h
OVERBAR_INC          := -I$(OVERBAR_DIR)/include
# the SDL client build (menu.c's SDL file listing and the MiSTer prescale's SDL types)
OVERBAR_DEFS         := -DOB_SDL_BASE
# the MiSTer filter files, copied next to the program
OVERBAR_MISTER       := $(OVERBAR_DIR)/mister
overbar_copy_mister   = mkdir -p "$(1)" && rm -rf "$(1)/mister" && cp -r "$(OVERBAR_MISTER)" "$(1)/mister"

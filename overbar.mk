# SPDX-License-Identifier: MIT
# Copyright (c) 2026 abdulahmad and contributors
#
# Overbar's sources, for a port that has Overbar as a git submodule and compiles it itself:
#
#   include native/overbar/overbar.mk
#   ... compile $(OVERBAR_SRCS) and $(OVERBAR_PULSEMAN_SRC) with the port's own compiler and
#   flags, plus $(OVERBAR_INC) $(OVERBAR_DEFS)
#
# third_party/pulseman/menu.c has the same file name as src/menu.c, so give its object another
# name (pulseman_menu.o). Shell build scripts read sources.txt, which lists the same files.

OVERBAR_DIR          ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
OVERBAR_LIB_NAMES    := scaler host present menu overlay filemenu
OVERBAR_SRCS         := $(OVERBAR_LIB_NAMES:%=$(OVERBAR_DIR)/src/%.c)
OVERBAR_PULSEMAN_SRC := $(OVERBAR_DIR)/third_party/pulseman/menu.c
OVERBAR_HDRS         := $(OVERBAR_DIR)/include/overbar/overbar.h $(OVERBAR_DIR)/include/mdrt/menu.h
OVERBAR_INC          := -I$(OVERBAR_DIR)/include
# the SDL client build (menu.c's SDL file listing and the MiSTer prescale's SDL types)
OVERBAR_DEFS         := -DOB_SDL_BASE

OPT_FLAGS := -O2 #-DDEBUG
#OPT_FLAGS := -O0 -DDEBUG

PLATFORM := osx
NAME     := nestalgia
#CC       := gcc
#CC       := clang
CC       := gcc-4.8
CFLAGS   := -g -DBPP32 $(OPT_FLAGS)
#CFLAGS   := -g $(OPT_FLAGS)
LDFLAGS  := -lreadline -largp `sdl-config --libs`
SOURCES  := $(wildcard platform/sdl/*.c)
INC_DIRS := platform/sdl
DEPS     := platform/$(PLATFORM).mk
HEADERS  := /usr/local/include/SDL/*

include platform/common.mk

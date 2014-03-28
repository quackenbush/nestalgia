OPT_FLAGS := -O2 -DDEBUG
#OPT_FLAGS := -O0 -DDEBUG
#OPT_FLAGS := -O3

PLATFORM := linux
NAME     := nestalgia
CC       := gcc
#CC       := clang
#CFLAGS   := -g -DBPP32 $(OPT_FLAGS)
CFLAGS   := -g $(OPT_FLAGS)
LDFLAGS  := -lreadline -lSDL
SOURCES  := $(wildcard platform/sdl/*.c)
INC_DIRS := platform/sdl
DEPS     := platform/$(PLATFORM).mk

include platform/common.mk

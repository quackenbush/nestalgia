CROSS_COMPILE := i586-mingw32msvc

PLATFORM := win32
NAME     := nestalgia.exe
CC       := $(CROSS_COMPILE)-gcc
LDFLAGS  := -lSDLmain -lSDL
CFLAGS   := -mwindows -DBPP32 -O3
DEFINES  := -DWINDOWS
SOURCES  := $(wildcard platform/sdl/*.c)
INC_DIRS := platform/sdl
DEPS     := platform/win32/$(PLATFORM).mk

RESOURCES := platform/win32/resource.rc

include platform/common.mk

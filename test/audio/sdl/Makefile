NAME := tone

CC := gcc
PERF_FLAGS := -O2 -DDEBUG
CFLAGS := -Wall -Werror -Wno-empty-body -Wstrict-prototypes -g $(PERF_FLAGS) --std=c99
LDFLAGS := -lm -lSDL
BUILD_DIR := .

SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))

DEPS := $(wildcard *.h) Makefile

OUTPUT := $(BUILD_DIR)/$(NAME)

all : $(OUTPUT)

$(shell mkdir -p $(BUILD_DIR))

$(BUILD_DIR)/%.o : %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT) : $(DEPS) $(OBJECTS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJECTS) -o $@

#clean :
#	rm -rf $(BUILD_DIR)

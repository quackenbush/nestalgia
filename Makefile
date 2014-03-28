SYSTEM := $(shell uname)

ifeq ($(SYSTEM),Darwin)
PLATFORM := osx
endif

ifeq ($(SYSTEM),Linux)
PLATFORM := linux
endif

all:
	$(MAKE) -f platform/$(PLATFORM).mk

clean:
	@rm -rf TAGS build/

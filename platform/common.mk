BUILD_DIR := build/$(PLATFORM)
LOCAL_DIR := build/local

QUIET := @

# ----------------------------------------
# Versionator
DOT_GIT = .gitversion

GIT_VERSION := $(shell git rev-list HEAD --count)
GIT := $(shell cat $(DOT_GIT) 2>/dev/null)
ifneq ($(GIT),$(GIT_VERSION))
    $(info Git version change [$(GIT_VERSION)] => forcing rebuild)
    $(shell rm -f $(DOT_GIT))
endif

$(DOT_GIT) :
	$(shell echo "$(GIT_VERSION)" > $(DOT_GIT))
# ----------------------------------------

COMMON_DIRS := . common gui fonts

SRC_DIRS  += $(COMMON_DIRS) mapper c64
INC_DIRS  += $(COMMON_DIRS)

C_INCDIRS += $(addprefix -I, $(INC_DIRS))
DEFINES   += -DVERSION=\"$(GIT_VERSION)\" -D$(PLATFORM)
WARNINGS  += -Wall -Werror -Wstrict-prototypes -Wuninitialized
WARNINGS  += -Wextra -Wno-empty-body -Wno-missing-field-initializers -Wno-unused-parameter
#WARNINGS  += -fsanitize=address
#WARNINGS  += -fsanitize=thread
CFLAGS    += $(WARNINGS) $(DEFINES) $(C_INCDIRS)

SOURCES   += $(wildcard $(addsuffix /*.c, $(SRC_DIRS)))

OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
OBJECTS += $(patsubst %.rc,$(BUILD_DIR)/%.o,$(RESOURCES))

HEADERS += $(wildcard $(addsuffix /*.h, $(SRC_DIRS)))

DEPS += $(DOT_GIT)
DEPS += platform/common.mk
DEPS += $(HEADERS)

OUTPUT := $(BUILD_DIR)/$(NAME)

all : TAGS $(OUTPUT) $(LOCAL_DIR)

$(shell mkdir -p $(BUILD_DIR))

$(BUILD_DIR)/%.o : %.c $(DEPS)
	@mkdir -p $(dir $@)
	$(info [ CC ] $@)
	$(QUIET) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o : %.rc $(DEPS)
	@mkdir -p $(dir $@)
	$(info [ RC ] $@)
	$(QUIET) $(CROSS_COMPILE)-windres $< -o $@

$(OUTPUT) : $(DEPS) $(OBJECTS)
	$(info [ LD ] $@)
	$(QUIET) $(CC) $(LDFLAGS) -lSDLmain $(OBJECTS) -o $@

$(LOCAL_DIR) : $(BUILD_DIR)
	$(QUIET) ln -sf $(PLATFORM) $(LOCAL_DIR)

tags : TAGS

TAGS : $(DEPS)
	$(info [ TAGS ])
	$(QUIET) etags $(SOURCES) $(HEADERS) -o $@

clean :
	rm -rf $(BUILD_DIR)

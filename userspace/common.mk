# This gives us an absolute path to work with no matter from where common.mk
# was included. At the beginning of this file, $(MAKEFILE_LIST) will always
# end  with common.mk, as common.mk was the last Makefile that was included.
COMMON_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# NOTE to users:
#
# This Makefile contains generic build commands for Tatix userspace programs.
# To use it, define the TARGET (name of the final executable) and SRCS (list
# of source files) variables in a Makefile in your program's source directory.
# Then `include path/to/common.mk` at the end. Typing `make` should now build
# all your source files into objects and link it with the CRT. Build artifacts
# will be stored in $(CWD)/build.

.PHONY += clean

CC := gcc
CFLAGS := -Wall -Wextra -pedantic -I$(COMMON_DIR)../include -fno-builtin

LD := ld
LDFLAGS := -T $(COMMON_DIR)common.ld

BUILD_DIR := build
OBJS := $(patsubst %, $(BUILD_DIR)/%.o, $(SRCS))

CRT := $(COMMON_DIR)crt/$(BUILD_DIR)/crt.c.o
CRT_DIR := $(COMMON_DIR)crt

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/%.c.o: %.c | $(BUILD_DIR)
	@echo CC $@
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS) | $(BUILD_DIR) $(CRT)
	@echo LD $@
	@$(LD) $(LDFLAGS) $^ $(CRT) -o $@

$(CRT): $(CRT_DIR)
	@echo "  MAKE $@"
	@make -C $<

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

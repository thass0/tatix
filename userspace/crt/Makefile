# The purpose of this Makefile is only to build the crt.o.c object
# so other programs can link it. It purposefully doesn't depend on
# common.mk because common.mk will automatically built the CRT with
# this Makefile.

BUILD_DIR := build

all: $(BUILD_DIR)/crt.c.o | $(BUILD_DIR)

$(BUILD_DIR)/crt.c.o: crt.c | $(BUILD_DIR)
	@ehco CC $@
	@gcc -Wall -Wextra -pedantic -I../../include -fno-builtin -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

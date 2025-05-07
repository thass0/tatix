# This Makefile drives compilation for all of Tatix.

################################################################################
# Variable definitions                                                         #
################################################################################

.PHONY = clean boot fmt

ifneq ($(DEBUG),)
    DEBUG_FLAGS := -g
else
	DEBUG := 0
endif

ifneq ($(GDB),)
	DEBUG_FLAGS := -g
    QEMU_DEBUG_FLAGS := -s -S
endif

CONFIG := config.mk
include $(CONFIG)

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

CC := gcc
CPPFLAGS := -MMD -Iinclude/
CFLAGS := $(DEBUG_FLAGS) -mgeneral-regs-only -std=gnu99 -ffreestanding -mcmodel=large -mno-red-zone -fno-builtin -nostdinc -Wall -Wextra -Wuninitialized -Wmaybe-uninitialized -pedantic

SRCS := $(shell find $(SRC_DIR) -type f -name "*.c")
OBJS := $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.d, $(filter %.c, $(SRCS)))

ROOTFS_DIR := rootfs/
ROOTFS_OBJ := $(BUILD_DIR)/rootfs.o
ROOTFS_ARCHIVE := $(BUILD_DIR)/rootfs.img

BOOTLOADER_SRCS := $(wildcard $(BOOTLOADER_DIR)/*)
BOOTLOADER_OBJS := $(patsubst $(BOOTLOADER_DIR)/%, $(BUILD_DIR)/%.o, $(BOOTLOADER_SRCS))

BOOTLOADER_IMAGE := $(BUILD_DIR)/bootloader.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
DISK_IMAGE := $(BUILD_DIR)/image.bin

LINKER_CONFIG := $(BUILD_DIR)/config.ld
NASM_CONFIG := $(BUILD_DIR)/config.s
HEADER_CONFIG := $(BUILD_DIR)/config.h

LINKER_PRINT_INFO := $(BUILD_DIR)/print_info.ld

################################################################################
# Function definitions                                                         #
################################################################################

# Arguments:
#   $(1): Name of the command
#   $(2): Output file(s)
define compile_log
	@echo "  $(1) $(2)"
endef

# The arguments to all of the `run_*` functions are:
#   $(1): output file(s),
#   $(2): intput file(s)
#   $(3): other arguments

define run_cat
	$(call compile_log,CAT,$(1))
	@cat $(2) > $(1)
endef

define run_ld
	$(call compile_log,LD,$(1))
	@ld $(3) -o $(1) $(2)
endef

define run_objcopy
	$(call compile_log,OBJCOPY,$(1))
	@objcopy $(3) $(2) $(1)
endef

define run_truncate
	$(call compile_log,TRUNCATE,$(1))
	@truncate $(2) $(1)
endef

define run_nasm
	$(call compile_log,NASM,$(1))
	@nasm -f elf64 $(3) $(2) -o $(1)
endef

define run_cc
	@mkdir -p $(dir $(1))
	$(call compile_log,CC,$(1))
	@$(CC) $(3) -c $(2) -o $(1)
endef

################################################################################
# Compilation                                                                  #
################################################################################

all: $(DISK_IMAGE)

$(DISK_IMAGE): $(BOOTLOADER_IMAGE) $(KERNEL_ELF) | $(BUILD_DIR)
	$(call run_cat,$@,$^)

################################################################################
# Compile bootloader                                                           #
################################################################################

$(BOOTLOADER_IMAGE): $(BOOTLOADER_OBJS) | $(BUILD_DIR) $(LINKER_CONFIG) bootloader.ld
	$(call run_ld,$(BUILD_DIR)/bootloader.elf,$^,-L$(BUILD_DIR) -T bootloader.ld --no-warn-rwx-segments)
	$(call run_objcopy,$@,$(BUILD_DIR)/bootloader.elf,-O binary)
	$(call run_truncate,$@,-s $(shell expr $(SECTOR_SIZE) \* $(BOOT_SECTOR_COUNT)),)

$(BUILD_DIR)/%.s.o: $(BOOTLOADER_DIR)/%.s | $(BUILD_DIR) $(NASM_CONFIG)
	$(call run_nasm,$@,$<,-I$(dir $(NASM_CONFIG)) -I$(BOOTLOADER_DIR))

$(BUILD_DIR)/%.c.o: $(BOOTLOADER_DIR)/%.c | $(BUILD_DIR) $(HEADER_CONFIG)
	$(call run_cc,$@,$<,$(CPPFLAGS) -I$(dir $(HEADER_CONFIG)) $(CFLAGS))

################################################################################
# Compile kernel                                                               #
################################################################################

$(KERNEL_ELF): $(OBJS) $(ROOTFS_OBJ) | $(BUILD_DIR) $(LINKER_CONFIG) $(LINKER_PRINT_INFO) kernel.ld
	$(call run_ld,$@,$^,-L$(BUILD_DIR) $(DEBUG_FLAGS) -T kernel.ld)

# To recompile if headers change:
-include $(DEPS)

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(HEADER_CONFIG)
	$(call run_cc,$@,$<,$(CPPFLAGS) -D__DEBUG__=$(DEBUG) -D__BASENAME__=\"$(notdir $<)\" -I$(dir $(HEADER_CONFIG)) $(CFLAGS))

$(BUILD_DIR)/%.s.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(call run_nasm,$@,$<)

################################################################################
# Build rootfs                                                                 #
################################################################################

# This will create an archive of the rootfs and place it into the .rootfs_archive section of a normal object.
# The linker will come looking for this later and put the section into the kernel ELF (see kernel.ld).
$(ROOTFS_OBJ): $(ROOTFS_ARCHIVE) | $(BUILD_DIR)
	$(call run_objcopy,$@,$<,-I binary -O elf64-x86-64 -B i386 --rename-section .data=.rootfs_archive,alloc,load,readonly,data,contents)

$(ROOTFS_ARCHIVE): $(ROOTFS_DIR) | $(BUILD_DIR)
	$(call compile_log,ARCHIVE,$@)
	@./scripts/archive.py enc $< $@

################################################################################
# Misc                                                                         #
################################################################################

$(LINKER_CONFIG): $(CONFIG)
	@./scripts/make_config.sh -f linker -o $@ $<

$(NASM_CONFIG): $(CONFIG)
	@./scripts/make_config.sh -f nasm -o $@ $<

$(HEADER_CONFIG): $(CONFIG)
	@./scripts/make_config.sh -f header -o $@ $<

$(LINKER_PRINT_INFO): $(OBJS) | $(BUILD_DIR)
	@echo "/* Auto-generated linker symbols */" > $@
	@echo "BASENAME_MAX_LEN = $$(for f in $(notdir $(SRCS)); do \
		echo -n "$$f" | wc -c; \
		done | sort -nr | head -1);" >> $@
	@echo "LINE_MAX_LEN = $$(wc -l --total=never $(SRCS) | sort -n | tail -1 | awk '{print length($$1)}');" >> $@
	@echo "FUNCNAME_MAX_LEN = $$(nm -C $(OBJS) | grep ' [tT] ' | awk '{ print length($$3) }' | sort -n | tail -1);" >> $@

$(BUILD_DIR):
	@mkdir $@

boot: $(DISK_IMAGE)
	@rm -f .packets.pcap
	@echo Starting VM
	@echo -----------
	@qemu-system-x86_64 -m 1G -cpu max -display none -serial stdio -no-reboot -drive file=$<,format=raw,index=0,media=disk \
	    -netdev tap,id=net0,ifname=vm0,script=no,downscript=no -device e1000,netdev=net0 \
		-object filter-dump,id=dump0,netdev=net0,file=.packets.pcap \
		$(QEMU_DEBUG_FLAGS)

fmt:
	clang-format -i --style=file $(SRCS) $(wildcard include/*.h)

clean:
	@$(RM) -r $(BUILD_DIR)
	@echo Deleted artifacts

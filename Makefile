# MiniDOS Makefile

CC = gcc
LD = ld
AS = nasm
OBJCOPY = objcopy
QEMU = qemu-system-i386
GDB = gdb

BOOT_DIR = src/boot
KERNEL_DIR = src/kernel
KERNEL_CORE_DIR = $(KERNEL_DIR)/core
KERNEL_DEBUG_DIR = $(KERNEL_DIR)/debug
KERNEL_INPUT_DIR = $(KERNEL_DIR)/input
KERNEL_MEMORY_DIR = $(KERNEL_DIR)/memory
KERNEL_PROCESS_DIR = $(KERNEL_DIR)/process
KERNEL_SHELL_DIR = $(KERNEL_DIR)/shell
KERNEL_STORAGE_DIR = $(KERNEL_DIR)/storage
KERNEL_TIME_DIR = $(KERNEL_DIR)/time
KERNEL_VIDEO_DIR = $(KERNEL_DIR)/video
BUILD_DIR = build
BOOTLOGO_DIR = assets/bootlogo
KERNEL_INCLUDE_DIRS := $(shell find $(KERNEL_DIR) -type d | sort)
CFLAGS_BASE = -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib $(addprefix -I,$(KERNEL_INCLUDE_DIRS))
CFLAGS = $(CFLAGS_BASE) $(EXTRA_CFLAGS)
LDFLAGS = -m elf_i386 -T $(KERNEL_CORE_DIR)/kernel.ld

KERNEL_SOURCE_DIRS = \
	$(KERNEL_CORE_DIR) \
	$(KERNEL_DEBUG_DIR) \
	$(KERNEL_INPUT_DIR) \
	$(KERNEL_MEMORY_DIR) \
	$(KERNEL_PROCESS_DIR) \
	$(KERNEL_SHELL_DIR) \
	$(KERNEL_STORAGE_DIR) \
	$(KERNEL_TIME_DIR) \
	$(KERNEL_VIDEO_DIR)
KERNEL_SOURCES_ALL := $(shell find $(KERNEL_SOURCE_DIRS) -name '*.c' | sort)
KERNEL_SOURCES = $(filter-out $(KERNEL_STORAGE_DIR)/fat12.c, $(KERNEL_SOURCES_ALL))
KERNEL_OBJECTS = $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(KERNEL_SOURCES))
KERNEL_ASM = $(BUILD_DIR)/core/entry.o $(BUILD_DIR)/video/backbuffer_fill.o
QEMU_FLOPPY_FLAGS = -drive file=minidos.img,format=raw,if=floppy,index=0 -boot a -m 16M -serial stdio

# Ensure drive.o is included

.PHONY: all clean run run-no-reboot run-trace run-gdb gdb-kernel verify-image ci phase0-check test-paging test-keyboard test-mouse test-phase3 test-phase4 full-test

all: minidos.img

run: minidos.img
	$(QEMU) $(QEMU_FLOPPY_FLAGS)

run-no-reboot: minidos.img
	$(QEMU) $(QEMU_FLOPPY_FLAGS) -no-reboot -no-shutdown

run-trace: minidos.img | $(BUILD_DIR)
	$(QEMU) $(QEMU_FLOPPY_FLAGS) -monitor none -display none -no-reboot -no-shutdown -d int,guest_errors,cpu_reset -D $(BUILD_DIR)/qemu-trace.log

run-gdb: minidos.img $(BUILD_DIR)/kernel.elf
	$(QEMU) $(QEMU_FLOPPY_FLAGS) -monitor none -display none -no-reboot -no-shutdown -S -gdb tcp::1234

gdb-kernel: $(BUILD_DIR)/kernel.elf scripts/kernel.gdb
	$(GDB) -x scripts/kernel.gdb

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Bootloader (floppy boot sector)
$(BUILD_DIR)/boot.bin: $(BOOT_DIR)/boot.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

# Second-stage bootloader
$(BUILD_DIR)/stage2.bin: $(BOOT_DIR)/stage2.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@ -l $(BUILD_DIR)/stage2.lst

# Kernel entry point (assembly)
$(BUILD_DIR)/core/entry.o: $(KERNEL_CORE_DIR)/entry.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/video/backbuffer_fill.o: $(KERNEL_DIR)/video/backbuffer_fill.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# Kernel objects
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel ELF with symbols
$(BUILD_DIR)/kernel.elf: $(KERNEL_ASM) $(KERNEL_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Kernel flat binary used by the bootloader
$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# Disk image - create 1.44MB FAT12 floppy image
minidos.img: $(BUILD_DIR)/boot.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin scripts/build_disk.sh
	@# Convert boot logo if BMP exists
	@if [ -f "$(BOOTLOGO_DIR)/boot_logo.bmp" ]; then \
		echo "Converting boot logo..."; \
		cd $(BOOTLOGO_DIR) && chmod +x convert_logo.sh && ./convert_logo.sh boot_logo.bmp logo.raw || true; \
	fi
	@chmod +x scripts/build_disk.sh
	@./scripts/build_disk.sh

clean:
	rm -rf $(BUILD_DIR) minidos.img

verify-image: minidos.img scripts/verify_image.sh
	bash scripts/verify_image.sh

# Test targets
test-help:
	./tests/test.sh help

test-ver:
	./tests/test.sh ver

test-drives:
	./tests/test.sh drives

test-dir:
	./tests/test.sh dir

test-rmdir:
	./tests/test.sh rmdir

test-runner:
	./tests/test_runner.sh "ver" "drives"

test-interactive:
	./tests/test_interactive.sh

test-serial: minidos.img
	python3 tests/test_serial.py "ver" "drives"

test-keyboard: minidos.img
	TMPDIR="$(CURDIR)/build" python3 tests/test_keyboard_irq.py

test-keyboard-soft: minidos.img
	TMPDIR="$(CURDIR)/build" python3 tests/test_keyboard_irq.py --soft-skip-env

test-mouse: minidos.img
	TMPDIR="$(CURDIR)/build" python3 tests/test_mouse_ui.py

test-phase3: minidos.img
	python3 tests/test_phase3.py

test-phase4: minidos.img
	python3 tests/test_phase4.py

full-test:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) verify-image
	$(MAKE) test
	$(MAKE) test-serial
	$(MAKE) test-keyboard-soft
	$(MAKE) test-paging
	$(MAKE) test-phase3
	$(MAKE) test-phase4

ci:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) verify-image
	$(MAKE) test-serial
	$(MAKE) test-keyboard-soft
	$(MAKE) test-paging
	$(MAKE) test-phase3
	$(MAKE) test-phase4

test: test-help test-ver test-drives

phase0-check:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) verify-image
	$(MAKE) test-serial

test-paging:
	$(MAKE) clean
	$(MAKE) all
	python3 tests/test_paging.py
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS=-DPAGING_TEST_PF all
	python3 tests/test_paging.py --expect-fault
	$(MAKE) clean
	$(MAKE) all

.PHONY: all clean run run-no-reboot run-trace run-gdb gdb-kernel verify-image ci phase0-check test-paging test-keyboard test-keyboard-soft test-mouse test-phase3 test-phase4 full-test test test-help test-ver test-drives test-dir test-rmdir test-runner test-interactive test-serial

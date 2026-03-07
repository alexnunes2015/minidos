# MiniDOS Makefile

CC = gcc
LD = ld
AS = nasm
CFLAGS_BASE = -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie -fno-common -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib -Isrc/kernel
CFLAGS = $(CFLAGS_BASE) $(EXTRA_CFLAGS)
LDFLAGS = -m elf_i386 -T src/kernel/kernel.ld

BOOT_DIR = src/boot
KERNEL_DIR = src/kernel
BUILD_DIR = build
BOOTLOGO_DIR = assets/bootlogo

KERNEL_SOURCES_ALL = $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_SOURCES = $(KERNEL_DIR)/kernel.c $(filter-out $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/fat12.c, $(KERNEL_SOURCES_ALL))
KERNEL_OBJECTS = $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(KERNEL_SOURCES))
KERNEL_ASM = $(BUILD_DIR)/entry.o

# Ensure drive.o is included

.PHONY: all clean run phase0-check test-paging test-keyboard test-phase3 test-phase4 full-test

all: minidos.img

run: minidos.img
	qemu-system-i386 -drive file=minidos.img,format=raw,if=floppy,index=0 -boot a -m 16M -serial stdio

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Bootloader (MBR)
$(BUILD_DIR)/boot.bin: $(BOOT_DIR)/boot.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

# Second-stage bootloader
$(BUILD_DIR)/stage2.bin: $(BOOT_DIR)/stage2.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@ -l $(BUILD_DIR)/stage2.lst

# Kernel entry point (assembly)
$(BUILD_DIR)/entry.o: $(KERNEL_DIR)/entry.asm | $(BUILD_DIR)
	$(AS) -f elf32 $< -o $@

# Kernel objects
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel binary
$(BUILD_DIR)/kernel.bin: $(KERNEL_ASM) $(KERNEL_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ --oformat binary $^

# Disk image - create with MBR and partitions
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

test-phase3: minidos.img
	python3 tests/test_phase3.py

test-phase4: minidos.img
	python3 tests/test_phase4.py

full-test:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) test
	$(MAKE) test-serial
	$(MAKE) test-keyboard-soft
	$(MAKE) test-paging
	$(MAKE) test-phase3
	$(MAKE) test-phase4

test: test-help test-ver test-drives

phase0-check:
	$(MAKE) clean
	$(MAKE) all
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

.PHONY: all clean run phase0-check test-paging test-keyboard test-keyboard-soft test-phase3 test-phase4 full-test test test-help test-ver test-drives test-dir test-rmdir test-runner test-interactive test-serial

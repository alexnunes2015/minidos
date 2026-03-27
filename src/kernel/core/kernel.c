#include "boot_splash.h"
#include "video.h"
#include "keyboard.h"
#include "shell/shell.h"
#include "disk.h"
#include "drive.h"
#include "fat16.h"
#include "serial.h"
#include "logger.h"
#include "paging.h"
#include "scheduler.h"
#include "timer.h"

// Memory size from BIOS (in KB)
unsigned int g_memory_kb = 0;

#define STOP_MEMORY_INVALID "STOP 0x00000001"
#define STOP_PAGING_INIT    "STOP 0x00000002"
#define STOP_DISK_READ      "STOP 0x00000003"
#define STOP_DRIVE_DETECT   "STOP 0x00000004"
#define STOP_SCHED_INIT     "STOP 0x00000006"
#define STOP_SCHED_SELFTEST "STOP 0x00000007"

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short read_phys_u16(unsigned int addr) {
    unsigned short val;
    __asm__ volatile ("movw (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static void bsod_wait_key_then_reboot() {
    timer_sleep_ms(1000);

    while (serial_received()) {
        (void)serial_getchar();
    }
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }

    while (1) {
        if (serial_received()) {
            (void)serial_getchar();
            break;
        }
        if (inb(0x64) & 0x01) {
            (void)inb(0x60);
            break;
        }
        timer_wait_for_interrupt();
    }

    outb(0x64, 0xFE);
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void boot_panic_bsod(const char* stop_code, const char* detail) {
    log_serial_raw("[boot] critical failure: ");
    if (stop_code) {
        log_serial_raw(stop_code);
        log_serial_raw(" ");
    }
    if (detail) {
        log_serial_raw(detail);
    } else {
        log_serial_raw("unknown error");
    }
    log_serial_raw("\n");
    video_show_bsod(stop_code, detail);
    bsod_wait_key_then_reboot();
}

/* Keep runtime shell handling out of the boot orchestrator without changing the link layout. */
#include "kernel_runtime_shell.inc"

void kernel_main() {
    unsigned int base_mem;
    unsigned int ext_mem;
    char mem_str[16];
    unsigned int mem;
    int idx = 0;

    // Initialize serial for debugging FIRST
    serial_init();
    log_init();
    log_serial_raw("\n\n=== MiniDOS Kernel Starting ===\n");

    // Read memory size from BIOS data (stored by bootloader)
    base_mem = read_phys_u16(0x500); // Base memory (up to 640KB)
    ext_mem = read_phys_u16(0x502);  // Extended memory (above 1MB)

    // Total memory = base + extended (extended is above 1MB, so add 1024KB)
    if (ext_mem > 0) {
        g_memory_kb = 1024 + ext_mem;  // Extended memory starts at 1MB
    } else {
        g_memory_kb = base_mem;         // Only base memory available
    }

    log_serial_raw("System Memory: ");
    mem = g_memory_kb;
    if (mem == 0) {
        mem_str[idx++] = '0';
    } else {
        int temp = (int)mem;
        int digits = 0;
        while (temp > 0) {
            temp /= 10;
            digits++;
        }
        idx = digits;
        temp = (int)mem;
        while (temp > 0) {
            mem_str[--digits] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    mem_str[idx] = '\0';
    log_serial_raw(mem_str);
    log_serial_raw(" KB\n");

    if (g_memory_kb == 0) {
        boot_panic_bsod(STOP_MEMORY_INVALID, "INVALID MEMORY SIZE REPORTED BY BIOS.");
    }

    if (paging_init() != 0) {
        boot_panic_bsod(STOP_PAGING_INIT, "FAILED TO INITIALIZE PAGING.");
    }

    log_write(LOG_LEVEL_INFO, "kernel", "MiniDOS v0.1 Kernel Started\n", LOG_DEST_SERIAL);
    boot_splash_begin();
    boot_splash_pump();

    log_write(LOG_LEVEL_INFO, "kernel", "Initializing disk driver...\n", LOG_DEST_SERIAL);
    disk_init();
    {
        unsigned char probe[512];
        if (disk_read_lba(0, probe) != 0) {
            boot_panic_bsod(STOP_DISK_READ, "FAILED TO READ BOOT DISK (LBA 0).");
        }
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Disk driver initialized\n", LOG_DEST_SERIAL);
    boot_splash_pump();

    drive_init_boot_media();
    boot_splash_try_load_logo();
    boot_splash_pump();

    log_write(LOG_LEVEL_INFO, "kernel", "Detecting drives and partitions...\n", LOG_DEST_SERIAL);
    drive_probe_additional();
    boot_splash_try_load_logo();
    boot_splash_pump();
    if (drive_get_count() <= 0) {
        boot_panic_bsod(STOP_DRIVE_DETECT, "NO VALID DRIVE DETECTED.");
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Drive detection complete\n", LOG_DEST_SERIAL);

    interrupts_init();
    if (scheduler_runtime_init() != 0) {
        boot_panic_bsod(STOP_SCHED_INIT, "FAILED TO INITIALIZE SCHEDULER RUNTIME.");
    }

    kernel_runtime_thread(0);
}

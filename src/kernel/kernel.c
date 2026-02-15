#include "video.h"
#include "keyboard.h"
#include "shell.h"
#include "disk.h"
#include "drive.h"
#include "fat16.h"
#include "serial.h"
#include "logger.h"
#include "paging.h"

// Memory size from BIOS (in KB)
unsigned int g_memory_kb = 0;
static const unsigned int BOOT_LOGO_MS = 5000;
static const unsigned int BOOT_SPLASH_MS = 1200;
static const unsigned int PIT_HZ = 1193182;

#define STOP_MEMORY_INVALID "STOP 0x00000001"
#define STOP_PAGING_INIT    "STOP 0x00000002"
#define STOP_DISK_READ      "STOP 0x00000003"
#define STOP_DRIVE_DETECT   "STOP 0x00000004"

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

// Simple delay function
static void delay(unsigned int count) {
    for (volatile unsigned int i = 0; i < count; i++) {
        for (volatile unsigned int j = 0; j < 10000; j++) {
            __asm__ volatile ("nop");
        }
    }
}

static unsigned short pit_read_counter0(void) {
    outb(0x43, 0x00); // Latch channel 0 current count
    unsigned short lo = inb(0x40);
    unsigned short hi = inb(0x40);
    return (unsigned short)(lo | (hi << 8));
}

static void pit_wait_ms(unsigned int ms) {
    unsigned int target = ms * (PIT_HZ / 1000);
    unsigned int elapsed = 0;
    unsigned short prev = pit_read_counter0();

    while (elapsed < target) {
        unsigned short cur = pit_read_counter0();
        unsigned short delta;
        if (prev >= cur) {
            delta = (unsigned short)(prev - cur);
        } else {
            delta = (unsigned short)(prev + (65536 - cur));
        }
        elapsed += (unsigned int)delta;
        prev = cur;
    }
}

static void wait_boot_logo() {
    const unsigned int step_ms = 50;
    unsigned int elapsed = 0;
    unsigned int frame = 0;

    video_draw_boot_gradient(0);
    while (elapsed < BOOT_LOGO_MS) {
        unsigned int next = elapsed + step_ms;
        if (next > BOOT_LOGO_MS) {
            next = BOOT_LOGO_MS;
        }

        pit_wait_ms(next - elapsed);
        elapsed = next;

        frame++;
        video_draw_boot_gradient(frame * 6);
    }
}

static void bsod_wait_key_then_reboot() {
    pit_wait_ms(1000);

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
        __asm__ volatile ("nop");
    }

    outb(0x64, 0xFE);
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void boot_panic_bsod(const char* stop_code, const char* detail) {
    log_serial_raw("[boot] critical failure: ");
    if (detail) {
        log_serial_raw(detail);
    } else {
        log_serial_raw("unknown error");
    }
    log_serial_raw("\n");
    video_show_bsod(stop_code, detail);
    bsod_wait_key_then_reboot();
}

// Display a simple boot splash before entering the shell
static void show_boot_logo() {
    if (video_is_graphics()) {
        static unsigned char logo_pixels[320 * 200];
        static unsigned char logo_palette[256 * 3];

        fat16_set_drive(drive_get_current());
        if (fat16_init()) {
            int logo_bytes = fat16_read_file("BOOTLOGO.DAT", logo_pixels, sizeof(logo_pixels));
            int pal_bytes = fat16_read_file("BOOTLOGO.PAL", logo_palette, sizeof(logo_palette));

            if (logo_bytes == (int)sizeof(logo_pixels) && pal_bytes == (int)sizeof(logo_palette)) {
                cls();
                video_draw_indexed_image_centered(logo_pixels, 320, 200, logo_palette);
                wait_boot_logo();
                cls();
                return;
            }
        }
    }

    cls();
    print_string("\n");
    print_string("========================================\n");
    if (video_is_graphics()) {
        print_string("      MiniDOS VESA Framebuffer Boot     \n");
    } else {
        print_string("           MiniDOS Text Boot            \n");
    }
    print_string("========================================\n");
    print_string("\nStarting kernel services...\n\n");

    print_string("[");
    for (int i = 0; i < 28; i++) {
        print_char('#');
        delay(2);
    }
    print_string("] READY\n");

    pit_wait_ms(BOOT_SPLASH_MS);
    cls();
}

void kernel_main() {
    // Initialize serial for debugging FIRST
    serial_init();
    log_init();
    log_serial_raw("\n\n=== MiniDOS Kernel Starting ===\n");
    
    // Read memory size from BIOS data (stored by bootloader)
    unsigned int base_mem = read_phys_u16(0x500); // Base memory (up to 640KB)
    unsigned int ext_mem = read_phys_u16(0x502);  // Extended memory (above 1MB)
    
    // Total memory = base + extended (extended is above 1MB, so add 1024KB)
    if (ext_mem > 0) {
        g_memory_kb = 1024 + ext_mem;  // Extended memory starts at 1MB
    } else {
        g_memory_kb = base_mem;         // Only base memory available
    }
    
    // Print memory size via serial
    log_serial_raw("System Memory: ");
    char mem_str[16];
    unsigned int mem = g_memory_kb;
    int idx = 0;
    if (mem == 0) {
        mem_str[idx++] = '0';
    } else {
        int temp = mem;
        int digits = 0;
        while (temp > 0) {
            temp /= 10;
            digits++;
        }
        idx = digits;
        temp = mem;
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
    interrupts_init();
    
    log_write(LOG_LEVEL_INFO, "kernel", "MiniDOS v0.1 Kernel Started\n", LOG_DEST_SERIAL);
    print_string("MiniDOS v0.1 Kernel Started\n");
    print_string("Welcome to your minimalist 16/32-bit OS.\n\n");
    
    log_write(LOG_LEVEL_INFO, "kernel", "Initializing disk driver...\n", LOG_DEST_SERIAL);
    disk_init();
    {
        unsigned char probe[512];
        if (disk_read_lba(0, probe) != 0) {
            boot_panic_bsod(STOP_DISK_READ, "FAILED TO READ BOOT DISK (LBA 0).");
        }
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Disk driver initialized\n", LOG_DEST_SERIAL);
    
    log_write(LOG_LEVEL_INFO, "kernel", "Detecting drives and partitions...\n", LOG_DEST_SERIAL);
    drive_init();
    if (drive_get_count() <= 0) {
        boot_panic_bsod(STOP_DRIVE_DETECT, "NO VALID DRIVE DETECTED.");
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Drive detection complete\n", LOG_DEST_SERIAL);

    show_boot_logo();
    
    log_write(LOG_LEVEL_INFO, "kernel", "Starting shell...\n", LOG_DEST_SERIAL);
    shell_init();
    
    log_write(LOG_LEVEL_INFO, "kernel", "Entering main loop\n", LOG_DEST_SERIAL);
    while(1) {
        shell_prompt();
        char command[64];
        int use_serial = 0;
        for (int i = 0; i < 50000; i++) {
            if (serial_received()) {
                use_serial = 1;
                break;
            }
            __asm__ volatile ("nop");
        }

        if (use_serial) {
            log_write(LOG_LEVEL_DEBUG, "input", "reading command from serial\n", LOG_DEST_SERIAL);
            serial_read_line(command, 64);
            log_serial_raw("\n");
        } else {
            log_write(LOG_LEVEL_TRACE, "input", "reading command from keyboard\n", LOG_DEST_SERIAL);
            keyboard_read_line(command, 64);
        }
        
        log_write(LOG_LEVEL_INFO, "shell", "Command: ", LOG_DEST_SERIAL);
        log_serial_raw(command);
        log_serial_raw("\n");
        
        shell_execute(command);
    }
}

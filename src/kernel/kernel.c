#include "video.h"
#include "keyboard.h"
#include "shell.h"
#include "disk.h"
#include "drive.h"
#include "vga.h"
#include "fat16.h"
#include "serial.h"

#define SERIAL_SHELL 1

// Memory size from BIOS (in KB)
unsigned int g_memory_kb = 0;

// Simple delay function
static void delay(unsigned int count) {
    for (volatile unsigned int i = 0; i < count; i++) {
        for (volatile unsigned int j = 0; j < 10000; j++) {
            __asm__ volatile ("nop");
        }
    }
}

// Display boot logo
static void show_boot_logo() {
    // Switch to VGA Mode 13h
    vga_mode13h_init();
    
    // Try to load logo from disk
    static unsigned char logo_buffer[64000];  // 320x200 = 64000 bytes
    
    // Initialize disk and FAT16
    disk_init();
    drive_init();
    
    // Try to read BOOTLOGO.DAT from drive A: (drive 0)
    fat16_set_drive(0);
    fat16_init();
    
    int bytes_read = fat16_read_file("BOOTLOGO.DAT", logo_buffer, sizeof(logo_buffer));
    
    if (bytes_read == 64000) {
        // Display the logo
        vga_display_image(logo_buffer);
        
        // Show for 5 seconds
        delay(500);  // ~5 seconds
    } else {
        // Fallback: show simple text screen
        vga_clear(1);  // Dark blue background
        
        // Just wait a bit
        delay(500);
    }
    
    // Return to text mode
    vga_text_mode();
}

void kernel_main() {
    // Initialize serial for debugging FIRST
    serial_init();
    serial_print("\n\n=== MiniDOS Kernel Starting ===\n");
    
    // Read memory size from BIOS data (stored by bootloader)
    unsigned short* base_mem_ptr = (unsigned short*)0x500;  // Base memory (up to 640KB)
    unsigned short* ext_mem_ptr = (unsigned short*)0x502;   // Extended memory (above 1MB)
    unsigned int base_mem = *base_mem_ptr;
    unsigned int ext_mem = *ext_mem_ptr;
    
    // Total memory = base + extended (extended is above 1MB, so add 1024KB)
    if (ext_mem > 0) {
        g_memory_kb = 1024 + ext_mem;  // Extended memory starts at 1MB
    } else {
        g_memory_kb = base_mem;         // Only base memory available
    }
    
    // Print memory size via serial
    serial_print("System Memory: ");
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
    serial_print(mem_str);
    serial_print(" KB\n");
    
    cls();
    print_string("MiniDOS v0.1 Kernel Started\n");
    print_string("Welcome to your minimalist 16/32-bit OS.\n\n");
    
    serial_print("Initializing disk driver...\n");
    disk_init();
    serial_print("Disk driver initialized\n");
    
    serial_print("Detecting drives and partitions...\n");
    drive_init();
    serial_print("Drive detection complete\n");
    
    serial_print("Starting shell...\n");
    shell_init();
    
    serial_print("Entering main loop\n");
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
            serial_print("[serial] ");
            serial_read_line(command, 64);
            serial_print("\n");
        } else {
            keyboard_read_line(command, 64);
        }
        
        serial_print("Command: ");
        serial_print(command);
        serial_print("\n");
        
        shell_execute(command);
    }
}

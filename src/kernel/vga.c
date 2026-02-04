#include "vga.h"

// VGA registers
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_WRITE 0x3C0
#define VGA_AC_READ 0x3C1
#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_DAC_READ_INDEX 0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA 0x3C9
#define VGA_MISC_READ 0x3CC
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_INSTAT_READ 0x3DA

// Port I/O functions
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Initialize VGA Mode 13h (320x200, 256 colors)
void vga_mode13h_init() {
    // Use BIOS interrupt (via inline assembly)
    __asm__ volatile (
        "pusha\n"
        "mov $0x0013, %%ax\n"  // AH=0x00 (set mode), AL=0x13 (mode 13h)
        "int $0x10\n"           // Call BIOS video interrupt
        "popa\n"
        :
        :
        : "eax"
    );
}

// Restore text mode (80x25, 16 colors)
void vga_text_mode() {
    __asm__ volatile (
        "pusha\n"
        "mov $0x0003, %%ax\n"  // AH=0x00 (set mode), AL=0x03 (text mode)
        "int $0x10\n"           // Call BIOS video interrupt
        "popa\n"
        :
        :
        : "eax"
    );
}

// Set VGA palette color (r, g, b: 0-63)
void vga_set_palette(unsigned char index, unsigned char r, unsigned char g, unsigned char b) {
    outb(VGA_DAC_WRITE_INDEX, index);
    outb(VGA_DAC_DATA, r);
    outb(VGA_DAC_DATA, g);
    outb(VGA_DAC_DATA, b);
}

// Draw pixel at (x, y) with color
void vga_put_pixel(int x, int y, unsigned char color) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        unsigned char* vga = (unsigned char*)VGA_MEMORY;
        vga[y * VGA_WIDTH + x] = color;
    }
}

// Display raw image (320x200 bytes)
void vga_display_image(const unsigned char* image_data) {
    unsigned char* vga = (unsigned char*)VGA_MEMORY;
    
    // Copy image data to VGA memory (64000 bytes)
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = image_data[i];
    }
}

// Clear screen with color
void vga_clear(unsigned char color) {
    unsigned char* vga = (unsigned char*)VGA_MEMORY;
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = color;
    }
}

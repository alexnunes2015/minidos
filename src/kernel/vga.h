#ifndef VGA_H
#define VGA_H

// VGA Mode 13h (320x200, 256 colors)
#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MEMORY 0xA0000

// Initialize VGA Mode 13h
void vga_mode13h_init();

// Restore text mode
void vga_text_mode();

// Set VGA palette color
void vga_set_palette(unsigned char index, unsigned char r, unsigned char g, unsigned char b);

// Draw pixel
void vga_put_pixel(int x, int y, unsigned char color);

// Display raw image (64000 bytes = 320x200)
void vga_display_image(const unsigned char* image_data);

// Clear screen with color
void vga_clear(unsigned char color);

#endif

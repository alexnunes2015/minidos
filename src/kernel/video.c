#include "video.h"

#define VIDEO_MEMORY (char*)0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

static int cursor_x = 0;
static int cursor_y = 0;

// I/O port functions for cursor
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void update_cursor() {
    unsigned short pos = cursor_y * SCREEN_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void cls() {
    char* video = VIDEO_MEMORY;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07; // Light grey on black
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void scroll() {
    char* video = VIDEO_MEMORY;
    for (int y = 0; y < SCREEN_HEIGHT - 1; y++) {
        for (int x = 0; x < SCREEN_WIDTH * 2; x++) {
            video[y * SCREEN_WIDTH * 2 + x] = video[(y + 1) * SCREEN_WIDTH * 2 + x];
        }
    }
    // Clear last line
    for (int x = 0; x < SCREEN_WIDTH * 2; x += 2) {
        video[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2 + x] = ' ';
        video[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH * 2 + x + 1] = 0x07;
    }
    cursor_y = SCREEN_HEIGHT - 1;
}

void print_char(char c) {
    char* video = VIDEO_MEMORY;
    
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            int offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    } else {
        int offset = (cursor_y * SCREEN_WIDTH + cursor_x) * 2;
        video[offset] = c;
        video[offset + 1] = 0x07;
        cursor_x++;
    }

    if (cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= SCREEN_HEIGHT) {
        scroll();
    }
    
    update_cursor();

}

void print_string(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
}

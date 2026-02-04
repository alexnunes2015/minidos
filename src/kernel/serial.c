#include "serial.h"

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Initialize serial port (COM1)
void serial_init() {
    outb(SERIAL_PORT + 1, 0x00);    // Disable all interrupts
    outb(SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_PORT + 1, 0x00);    //                  (hi byte)
    outb(SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

// Check if transmit buffer is empty
static int serial_is_transmit_empty() {
    return inb(SERIAL_PORT + 5) & 0x20;
}

// Check if data is available to read
int serial_received() {
    return inb(SERIAL_PORT + 5) & 0x01;
}

// Read character from serial (blocking)
char serial_getchar() {
    while (!serial_received());
    return inb(SERIAL_PORT);
}

// Write character to serial
void serial_putchar(char c) {
    // Wait for transmit buffer to be empty
    while (!serial_is_transmit_empty());
    outb(SERIAL_PORT, c);
}

// Write string to serial
void serial_print(const char* str) {
    while (*str) {
        serial_putchar(*str++);
    }
}

// Write hex number to serial
void serial_print_hex(unsigned int num) {
    char hex[] = "0123456789ABCDEF";
    serial_print("0x");
    
    for (int i = 28; i >= 0; i -= 4) {
        serial_putchar(hex[(num >> i) & 0xF]);
    }
}

// Read a line from serial into buffer
void serial_read_line(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = serial_getchar();
        if (c == '\r' || c == '\n') {
            break;
        }
        buffer[i++] = c;
        // Echo to screen/serial for visibility
        serial_putchar(c);
    }
    buffer[i] = '\0';
}

#ifndef SERIAL_H
#define SERIAL_H

// COM1 port
#define SERIAL_PORT 0x3F8

// Initialize serial port
void serial_init();

// Write character to serial
void serial_putchar(char c);

// Write string to serial
void serial_print(const char* str);

// Write hex number to serial
void serial_print_hex(unsigned int num);

// Check if data is available to read
int serial_received();

// Read character from serial (blocking)
char serial_getchar();

// Read a line from serial into buffer
void serial_read_line(char* buffer, int max_len);

#endif

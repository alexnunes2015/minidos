#include "keyboard.h"
#include "video.h"

// I/O Port functions
static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Keyboard state
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int caps_lock = 0;
static int break_code = 0;
static int extended_code = 0;
static int scancode_set2 = 0;

// Scancode to ASCII mapping (lowercase)
static const char scancode_map_lower[] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  '-',  '=',  '\b',
    '\t', 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  'o',  'p',  '[',  ']',  '\n',
    0,    'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  '\'', '`',  0,
    '\\', 'z',  'x',  'c',  'v',  'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  0,    ' '
};

// Scancode to ASCII mapping (with SHIFT)
static const char scancode_map_upper[] = {
    0,    27,   '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',  '_',  '+',  '\b',
    '\t', 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}',  '\n',
    0,    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  '"',  '~',  0,
    '|',  'Z',  'X',  'C',  'V',  'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',  0,    ' '
};

static char scancode_to_char(unsigned char scancode, int shift) {
    const char* map = shift ? scancode_map_upper : scancode_map_lower;
    if (scancode < (sizeof(scancode_map_lower) / sizeof(scancode_map_lower[0]))) {
        return map[scancode];
    }
    return 0;
}

static char apply_caps(char c, int shift, int caps) {
    if (c >= 'a' && c <= 'z') {
        if (caps && !shift) return (char)(c - 'a' + 'A');
        return c;
    }
    if (c >= 'A' && c <= 'Z') {
        if (caps && shift) return (char)(c - 'A' + 'a');
        return c;
    }
    return c;
}

char keyboard_get_char() {
    while (1) {
        if (inb(0x64) & 0x01) { // Data available
            unsigned char scancode = inb(0x60);

            // Extended scancode prefix
            if (scancode == 0xE0) {
                extended_code = 1;
                continue;
            }

            // Break code prefix (set 2)
            if (scancode == 0xF0) {
                scancode_set2 = 1;
                break_code = 1;
                continue;
            }
            
            // Handle special keys (scancodes > 0x80 are key releases)
            if (scancode == 0x2A || scancode == 0x36) {  // Left or Right Shift pressed (set 1)
                shift_pressed = 1;
                continue;
            }
            if (scancode == 0xAA || scancode == 0xB6) {  // Left or Right Shift released (set 1)
                shift_pressed = 0;
                continue;
            }

            // Shift in set 2 (make/break)
            if (scancode_set2 && (scancode == 0x12 || scancode == 0x59)) {
                if (break_code) {
                    shift_pressed = 0;
                } else {
                    shift_pressed = 1;
                }
                break_code = 0;
                extended_code = 0;
                continue;
            }
            if (scancode == 0x1D) {  // Left Ctrl pressed
                ctrl_pressed = 1;
                continue;
            }
            if (scancode == 0x9D) {  // Left Ctrl released
                ctrl_pressed = 0;
                continue;
            }

            // Caps Lock toggle (set 1 / set 2)
            if ((scancode == 0x3A) || (scancode_set2 && scancode == 0x58)) {
                // Only toggle on key press (not release)
                if (!break_code && !(scancode & 0x80)) {
                    caps_lock = !caps_lock;
                }
                break_code = 0;
                extended_code = 0;
                continue;
            }
            
            // Regular key press (not release)
            if (!(scancode & 0x80) && !break_code) {
                char c = scancode_to_char(scancode, shift_pressed);
                c = apply_caps(c, shift_pressed, caps_lock);
                if (c) {
                    extended_code = 0;
                    return c;
                }
            }

            if (scancode_set2) {
                break_code = 0;
            }
            extended_code = 0;
        }
    }
}

void keyboard_read_line(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_get_char();
        if (c == '\n') {
            print_char('\n');
            buffer[i] = '\0';
            return;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                print_char('\b');
            }
        } else {
            buffer[i++] = c;
            print_char(c);
        }
    }
    buffer[i] = '\0';
}

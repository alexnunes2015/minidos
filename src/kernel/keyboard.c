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
static int alt_pressed = 0;
static int caps_lock = 0;
static int break_code = 0;
static int extended_code = 0;
static int scancode_set2 = 0;
static int irq_mode = 0;
static char key_buffer[128];
static int key_head = 0;
static int key_tail = 0;

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

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static void push_char(char c) {
    int next = (key_head + 1) % (int)sizeof(key_buffer);
    if (next == key_tail) {
        return;
    }
    key_buffer[key_head] = c;
    key_head = next;
}

static int pop_char(char* out) {
    if (key_tail == key_head) {
        return 0;
    }
    *out = key_buffer[key_tail];
    key_tail = (key_tail + 1) % (int)sizeof(key_buffer);
    return 1;
}

static char keyboard_process_scancode(unsigned char scancode) {
    if (scancode == 0xE0) {
        extended_code = 1;
        return 0;
    }

    if (scancode == 0xF0) {
        scancode_set2 = 1;
        break_code = 1;
        return 0;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return 0;
    }

    if (scancode_set2 && (scancode == 0x12 || scancode == 0x59)) {
        if (break_code) {
            shift_pressed = 0;
        } else {
            shift_pressed = 1;
        }
        break_code = 0;
        extended_code = 0;
        return 0;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return 0;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return 0;
    }
    if (!scancode_set2 && scancode == 0x38) {
        alt_pressed = 1;
        return 0x14; // KEY_ALT_TOGGLE
    }
    if (!scancode_set2 && scancode == 0xB8) {
        alt_pressed = 0;
        return 0;
    }
    if (scancode_set2 && scancode == 0x11) {
        if (break_code) {
            alt_pressed = 0;
        } else {
            alt_pressed = 1;
        }
        break_code = 0;
        extended_code = 0;
        return 0;
    }

    if ((scancode == 0x3A) || (scancode_set2 && scancode == 0x58)) {
        if (!break_code && !(scancode & 0x80)) {
            caps_lock = !caps_lock;
        }
        break_code = 0;
        extended_code = 0;
        return 0;
    }

    if (extended_code && !break_code) {
        if (scancode == 0x48 || scancode == 0x75) {
            extended_code = 0;
            if (scancode_set2) {
                break_code = 0;
            }
            return 0x11; // KEY_UP
        }
        if (scancode == 0x50 || scancode == 0x72) {
            extended_code = 0;
            if (scancode_set2) {
                break_code = 0;
            }
            return 0x12; // KEY_DOWN
        }
        if (scancode == 0x4B || scancode == 0x6B) {
            extended_code = 0;
            if (scancode_set2) {
                break_code = 0;
            }
            return 0x15; // KEY_LEFT
        }
        if (scancode == 0x4D || scancode == 0x74) {
            extended_code = 0;
            if (scancode_set2) {
                break_code = 0;
            }
            return 0x16; // KEY_RIGHT
        }
    }

    if (!(scancode & 0x80) && !break_code) {
        char c = scancode_to_char(scancode, shift_pressed);
        c = apply_caps(c, shift_pressed, caps_lock);
        if (c) {
            if (alt_pressed) {
                char lower = to_lower_char(c);
                if (lower >= 'a' && lower <= 'z') {
                    c = (char)(0x80 | lower);
                }
            }
            extended_code = 0;
            if (scancode_set2) {
                break_code = 0;
            }
            return c;
        }
    }

    if (scancode_set2) {
        break_code = 0;
    }
    extended_code = 0;
    return 0;
}

void keyboard_init(void) {
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
    break_code = 0;
    extended_code = 0;
    scancode_set2 = 0;
    irq_mode = 0;
    key_head = 0;
    key_tail = 0;
}

void keyboard_set_irq_mode(int enabled) {
    irq_mode = enabled ? 1 : 0;
}

void keyboard_handle_irq(void) {
    if (inb(0x64) & 0x01) {
        unsigned char scancode = inb(0x60);
        char c = keyboard_process_scancode(scancode);
        if (c) {
            push_char(c);
        }
    }
}

char keyboard_get_char() {
    char c;
    while (1) {
        if (pop_char(&c)) {
            return c;
        }

        if (!irq_mode && (inb(0x64) & 0x01)) {
            unsigned char scancode = inb(0x60);
            c = keyboard_process_scancode(scancode);
            if (c) {
                return c;
            }
        }

        __asm__ volatile ("nop");
    }
}

int keyboard_try_get_char(char* out) {
    char c;
    if (!out) {
        return 0;
    }

    if (pop_char(&c)) {
        *out = c;
        return 1;
    }

    if (!irq_mode && (inb(0x64) & 0x01)) {
        unsigned char scancode = inb(0x60);
        c = keyboard_process_scancode(scancode);
        if (c) {
            *out = c;
            return 1;
        }
    }

    return 0;
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

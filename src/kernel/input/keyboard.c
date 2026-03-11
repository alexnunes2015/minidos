#include "keyboard.h"
#include "logger.h"
#include "video.h"
#include "timer.h"

// I/O Port functions
static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Keyboard state
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;
static int break_code = 0;
static int extended_code = 0;
static int scancode_set = 1;
static int irq_mode = 0;
static unsigned int last_irq_tick = 0;
static int irq_seen = 0;
static unsigned char last_make_scancode = 0;
static int last_make_extended = 0;
static unsigned int last_make_tick = 0;
static int last_make_valid = 0;
static char key_buffer[128];
static int key_head = 0;
static int key_tail = 0;

#define KEYBOARD_POLL_FALLBACK_IDLE_TICKS 2U
#define KEYBOARD_DUPLICATE_MAKE_TICKS 2U

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

static const char scancode_set2_map_lower[128] = {
    [0x0D] = '\t',
    [0x0E] = '`',
    [0x15] = 'q',
    [0x16] = '1',
    [0x1A] = 'z',
    [0x1B] = 's',
    [0x1C] = 'a',
    [0x1D] = 'w',
    [0x1E] = '2',
    [0x21] = 'c',
    [0x22] = 'x',
    [0x23] = 'd',
    [0x24] = 'e',
    [0x25] = '4',
    [0x26] = '3',
    [0x29] = ' ',
    [0x2A] = 'v',
    [0x2B] = 'f',
    [0x2C] = 't',
    [0x2D] = 'r',
    [0x2E] = '5',
    [0x31] = 'n',
    [0x32] = 'b',
    [0x33] = 'h',
    [0x34] = 'g',
    [0x35] = 'y',
    [0x36] = '6',
    [0x3A] = 'm',
    [0x3B] = 'j',
    [0x3C] = 'u',
    [0x3D] = '7',
    [0x3E] = '8',
    [0x41] = ',',
    [0x42] = 'k',
    [0x43] = 'i',
    [0x44] = 'o',
    [0x45] = '0',
    [0x46] = '9',
    [0x49] = '.',
    [0x4A] = '/',
    [0x4B] = 'l',
    [0x4C] = ';',
    [0x4D] = 'p',
    [0x4E] = '-',
    [0x52] = '\'',
    [0x54] = '[',
    [0x55] = '=',
    [0x5A] = '\n',
    [0x5B] = ']',
    [0x5D] = '\\',
    [0x66] = '\b',
    [0x76] = 27
};

static const char scancode_set2_map_upper[128] = {
    [0x0D] = '\t',
    [0x0E] = '~',
    [0x15] = 'Q',
    [0x16] = '!',
    [0x1A] = 'Z',
    [0x1B] = 'S',
    [0x1C] = 'A',
    [0x1D] = 'W',
    [0x1E] = '@',
    [0x21] = 'C',
    [0x22] = 'X',
    [0x23] = 'D',
    [0x24] = 'E',
    [0x25] = '$',
    [0x26] = '#',
    [0x29] = ' ',
    [0x2A] = 'V',
    [0x2B] = 'F',
    [0x2C] = 'T',
    [0x2D] = 'R',
    [0x2E] = '%',
    [0x31] = 'N',
    [0x32] = 'B',
    [0x33] = 'H',
    [0x34] = 'G',
    [0x35] = 'Y',
    [0x36] = '^',
    [0x3A] = 'M',
    [0x3B] = 'J',
    [0x3C] = 'U',
    [0x3D] = '&',
    [0x3E] = '*',
    [0x41] = '<',
    [0x42] = 'K',
    [0x43] = 'I',
    [0x44] = 'O',
    [0x45] = ')',
    [0x46] = '(',
    [0x49] = '>',
    [0x4A] = '?',
    [0x4B] = 'L',
    [0x4C] = ':',
    [0x4D] = 'P',
    [0x4E] = '_',
    [0x52] = '"',
    [0x54] = '{',
    [0x55] = '+',
    [0x5A] = '\n',
    [0x5B] = '}',
    [0x5D] = '|',
    [0x66] = '\b',
    [0x76] = 27
};

static char scancode_set1_to_char(unsigned char scancode, int shift) {
    const char* map = shift ? scancode_map_upper : scancode_map_lower;
    if (scancode < (sizeof(scancode_map_lower) / sizeof(scancode_map_lower[0]))) {
        return map[scancode];
    }
    return 0;
}

static char scancode_set2_to_char(unsigned char scancode, int shift) {
    const char* map = shift ? scancode_set2_map_upper : scancode_set2_map_lower;
    if (scancode < 128U) {
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

static int keyboard_allow_poll_fallback(void) {
    if (!irq_mode) {
        return 1;
    }

    if (!irq_seen) {
        return 1;
    }

    if (!timer_is_ready()) {
        return 0;
    }

    return (unsigned int)(timer_get_ticks() - last_irq_tick) >= KEYBOARD_POLL_FALLBACK_IDLE_TICKS;
}

static int keyboard_is_duplicate_make(unsigned char scancode, int extended, int break_event) {
    if (break_event) {
        if (last_make_valid &&
            last_make_scancode == scancode &&
            last_make_extended == extended) {
            last_make_valid = 0;
        }
        return 0;
    }

    if (last_make_valid &&
        last_make_scancode == scancode &&
        last_make_extended == extended &&
        timer_is_ready() &&
        (unsigned int)(timer_get_ticks() - last_make_tick) <= KEYBOARD_DUPLICATE_MAKE_TICKS) {
        return 1;
    }

    last_make_scancode = scancode;
    last_make_extended = extended;
    last_make_valid = 1;
    if (timer_is_ready()) {
        last_make_tick = timer_get_ticks();
    } else {
        last_make_tick = 0;
    }
    return 0;
}

static int keyboard_wait_input_empty(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            return 1;
        }
    }
    return 0;
}

static int keyboard_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) {
            return 1;
        }
    }
    return 0;
}

static unsigned char keyboard_read_controller_command_byte(void) {
    if (!keyboard_wait_input_empty()) {
        return 0x40;
    }

    outb(0x64, 0x20);
    if (!keyboard_wait_output_full()) {
        return 0x40;
    }

    return inb(0x60);
}

static char keyboard_process_scancode_set1(unsigned char scancode) {
    if (scancode == 0xE0) {
        extended_code = 1;
        return 0;
    }

    if (extended_code) {
        if (scancode == 0x48) {
            extended_code = 0;
            return 0x11; // KEY_UP
        }
        if (scancode == 0x50) {
            extended_code = 0;
            return 0x12; // KEY_DOWN
        }
        if (scancode == 0x4B) {
            extended_code = 0;
            return 0x15; // KEY_LEFT
        }
        if (scancode == 0x4D) {
            extended_code = 0;
            return 0x16; // KEY_RIGHT
        }
        if (scancode == 0x1D) {
            ctrl_pressed = 1;
            extended_code = 0;
            return 0;
        }
        if (scancode == 0x9D) {
            ctrl_pressed = 0;
            extended_code = 0;
            return 0;
        }
        if (scancode == 0x38) {
            alt_pressed = 1;
            extended_code = 0;
            return 0;
        }
        if (scancode == 0xB8) {
            alt_pressed = 0;
            extended_code = 0;
            return 0;
        }
        extended_code = 0;
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
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return 0;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return 0;
    }
    if (scancode == 0x38) {
        alt_pressed = 1;
        return 0;
    }
    if (scancode == 0xB8) {
        alt_pressed = 0;
        return 0;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return 0;
    }

    if (scancode & 0x80) {
        return 0;
    }

    {
        char c = scancode_set1_to_char(scancode, shift_pressed);
        c = apply_caps(c, shift_pressed, caps_lock);
        if (c && alt_pressed) {
            char lower = to_lower_char(c);
            if (lower >= 'a' && lower <= 'z') {
                c = (char)(0x80 | lower);
            }
        }
        return c;
    }
}

static char keyboard_process_scancode_set2(unsigned char scancode) {
    if (scancode == 0xE0) {
        extended_code = 1;
        return 0;
    }

    if (scancode == 0xF0) {
        break_code = 1;
        return 0;
    }

    if (extended_code) {
        if (scancode == 0x11) {
            alt_pressed = break_code ? 0 : 1;
            break_code = 0;
            extended_code = 0;
            return 0;
        }
        if (scancode == 0x14) {
            ctrl_pressed = break_code ? 0 : 1;
            break_code = 0;
            extended_code = 0;
            return 0;
        }
        if (!break_code) {
            if (scancode == 0x75) {
                extended_code = 0;
                return 0x11; // KEY_UP
            }
            if (scancode == 0x72) {
                extended_code = 0;
                return 0x12; // KEY_DOWN
            }
            if (scancode == 0x6B) {
                extended_code = 0;
                return 0x15; // KEY_LEFT
            }
            if (scancode == 0x74) {
                extended_code = 0;
                return 0x16; // KEY_RIGHT
            }
            if (scancode == 0x5A) {
                extended_code = 0;
                return '\n';
            }
        }
        break_code = 0;
        extended_code = 0;
        return 0;
    }

    if (scancode == 0x12 || scancode == 0x59) {
        shift_pressed = break_code ? 0 : 1;
        break_code = 0;
        return 0;
    }
    if (scancode == 0x14) {
        ctrl_pressed = break_code ? 0 : 1;
        break_code = 0;
        return 0;
    }
    if (scancode == 0x11) {
        alt_pressed = break_code ? 0 : 1;
        break_code = 0;
        return 0;
    }
    if (scancode == 0x58) {
        if (!break_code) {
            caps_lock = !caps_lock;
        }
        break_code = 0;
        return 0;
    }

    if (break_code) {
        break_code = 0;
        return 0;
    }

    {
        char c = scancode_set2_to_char(scancode, shift_pressed);
        c = apply_caps(c, shift_pressed, caps_lock);
        if (c && alt_pressed) {
            char lower = to_lower_char(c);
            if (lower >= 'a' && lower <= 'z') {
                c = (char)(0x80 | lower);
            }
        }
        return c;
    }
}

static char keyboard_process_scancode(unsigned char scancode) {
    if (scancode_set == 2) {
        return keyboard_process_scancode_set2(scancode);
    }
    return keyboard_process_scancode_set1(scancode);
}

static int keyboard_poll_one_char(char* out) {
    char c;
    unsigned char status;

    if (!keyboard_allow_poll_fallback()) {
        return 0;
    }

    status = inb(0x64);
    if ((status & 0x01) == 0 || (status & 0x20) != 0) {
        return 0;
    }

    {
        unsigned char scancode = inb(0x60);
        int extended = extended_code;
        int break_event = 0;
        int track_scancode = 1;

        if (scancode_set == 2) {
            if (scancode != 0xE0 && scancode != 0xF0) {
                break_event = break_code;
            } else {
                track_scancode = 0;
            }
        } else if (scancode != 0xE0) {
            break_event = (scancode & 0x80) != 0;
        } else {
            track_scancode = 0;
        }

        c = keyboard_process_scancode(scancode);
        if (c && (!track_scancode || !keyboard_is_duplicate_make(scancode, extended, break_event))) {
            if (out) {
                *out = c;
            }
            return 1;
        }
        if (!c && track_scancode) {
            (void)keyboard_is_duplicate_make(scancode, extended, break_event);
        }
    }

    return 0;
}

void keyboard_init(void) {
    unsigned char command_byte = keyboard_read_controller_command_byte();

    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
    break_code = 0;
    extended_code = 0;
    scancode_set = (command_byte & 0x40U) ? 1 : 2;
    irq_mode = 0;
    last_irq_tick = 0;
    irq_seen = 0;
    last_make_scancode = 0;
    last_make_extended = 0;
    last_make_tick = 0;
    last_make_valid = 0;
    key_head = 0;
    key_tail = 0;

    if (scancode_set == 2) {
        log_serial_raw("[kbd] scan set 2 selected (translation off)\n");
    } else {
        log_serial_raw("[kbd] scan set 1 selected (translation on)\n");
    }
}

void keyboard_flush(void) {
    /* Drain hardware PS/2 output buffer */
    int safety = 512;
    while ((inb(0x64) & 0x01) && safety-- > 0) {
        (void)inb(0x60);
    }
    /* Clear software key buffer */
    key_head = 0;
    key_tail = 0;
    /* Reset modifier state to avoid stuck keys */
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    break_code = 0;
    extended_code = 0;
    last_make_valid = 0;
}

void keyboard_set_irq_mode(int enabled) {
    irq_mode = enabled ? 1 : 0;
}

void keyboard_handle_irq(void) {
    unsigned char status = inb(0x64);

    if ((status & 0x01) != 0 && (status & 0x20) == 0) {
        unsigned char scancode = inb(0x60);
        int extended = extended_code;
        int break_event = 0;
        int track_scancode = 1;
        if (scancode_set == 2) {
            if (scancode != 0xE0 && scancode != 0xF0) {
                break_event = break_code;
            } else {
                track_scancode = 0;
            }
        } else if (scancode != 0xE0) {
            break_event = (scancode & 0x80) != 0;
        } else {
            track_scancode = 0;
        }
        char c = keyboard_process_scancode(scancode);

        irq_seen = 1;
        if (timer_is_ready()) {
            last_irq_tick = timer_get_ticks();
        }
        if (c && (!track_scancode || !keyboard_is_duplicate_make(scancode, extended, break_event))) {
            push_char(c);
        } else if (!c && track_scancode) {
            (void)keyboard_is_duplicate_make(scancode, extended, break_event);
        }
    }
}

char keyboard_get_char() {
    char c;
    while (1) {
        if (pop_char(&c)) {
            return c;
        }

        /* Use direct controller polling only when IRQ delivery is disabled or has gone idle. */
        if (keyboard_poll_one_char(&c)) {
            return c;
        }

        timer_wait_for_interrupt();
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

    if (keyboard_poll_one_char(&c)) {
        *out = c;
        return 1;
    }

    return 0;
}

int keyboard_has_input(void) {
    char c = 0;

    if (key_tail != key_head) {
        return 1;
    }

    if (keyboard_poll_one_char(&c)) {
        push_char(c);
        return 1;
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

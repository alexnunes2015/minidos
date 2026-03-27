#ifndef INPUT_PS2_CONTROLLER_H
#define INPUT_PS2_CONTROLLER_H

#define PS2_PORT_DATA 0x60U
#define PS2_PORT_STATUS 0x64U

#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL 0x02U
#define PS2_STATUS_AUX_DATA 0x20U

#define PS2_CMD_READ_COMMAND_BYTE 0x20U
#define PS2_CMD_WRITE_COMMAND_BYTE 0x60U
#define PS2_CMD_ENABLE_AUX_PORT 0xA8U
#define PS2_CMD_WRITE_AUX_DATA 0xD4U

static inline unsigned char ps2_inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ps2_outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char ps2_read_status(void) {
    return ps2_inb(PS2_PORT_STATUS);
}

static inline int ps2_wait_input_empty(void) {
    for (int i = 0; i < 100000; i++) {
        if ((ps2_read_status() & PS2_STATUS_INPUT_FULL) == 0) {
            return 1;
        }
    }
    return 0;
}

static inline int ps2_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (ps2_read_status() & PS2_STATUS_OUTPUT_FULL) {
            return 1;
        }
    }
    return 0;
}

static inline void ps2_drain_output(int max_reads) {
    for (int i = 0; i < max_reads; i++) {
        if ((ps2_read_status() & PS2_STATUS_OUTPUT_FULL) == 0) {
            break;
        }
        (void)ps2_inb(PS2_PORT_DATA);
    }
}

static inline int ps2_read_data(unsigned char* out) {
    if (!out || !ps2_wait_output_full()) {
        return 0;
    }
    *out = ps2_inb(PS2_PORT_DATA);
    return 1;
}

static inline unsigned char ps2_read_controller_command_byte(unsigned char fallback) {
    if (!ps2_wait_input_empty()) {
        return fallback;
    }
    ps2_outb(PS2_PORT_STATUS, PS2_CMD_READ_COMMAND_BYTE);
    if (!ps2_wait_output_full()) {
        return fallback;
    }
    return ps2_inb(PS2_PORT_DATA);
}

static inline int ps2_write_controller_command_byte(unsigned char value) {
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    ps2_outb(PS2_PORT_STATUS, PS2_CMD_WRITE_COMMAND_BYTE);
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    ps2_outb(PS2_PORT_DATA, value);
    return 1;
}

static inline int ps2_write_aux_device(unsigned char value) {
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    ps2_outb(PS2_PORT_STATUS, PS2_CMD_WRITE_AUX_DATA);
    if (!ps2_wait_input_empty()) {
        return 0;
    }
    ps2_outb(PS2_PORT_DATA, value);
    return 1;
}

#endif

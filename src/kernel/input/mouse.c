#include "mouse.h"
#include "logger.h"
#include "video.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static mouse_state_t g_mouse_state;
static unsigned char g_packet[3];
static int g_packet_index = 0;
static int g_packet_logged = 0;

static int mouse_wait_input_empty(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            return 1;
        }
    }
    return 0;
}

static int mouse_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) {
            return 1;
        }
    }
    return 0;
}

static void mouse_drain_output(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(0x64) & 0x01)) {
            break;
        }
        (void)inb(0x60);
    }
}

static unsigned char mouse_read_controller_command_byte(void) {
    if (!mouse_wait_input_empty()) {
        return 0x00;
    }
    outb(0x64, 0x20);
    if (!mouse_wait_output_full()) {
        return 0x00;
    }
    return inb(0x60);
}

static int mouse_write_controller_command_byte(unsigned char value) {
    if (!mouse_wait_input_empty()) {
        return 0;
    }
    outb(0x64, 0x60);
    if (!mouse_wait_input_empty()) {
        return 0;
    }
    outb(0x60, value);
    return 1;
}

static int mouse_write_device(unsigned char value) {
    if (!mouse_wait_input_empty()) {
        return 0;
    }
    outb(0x64, 0xD4);
    if (!mouse_wait_input_empty()) {
        return 0;
    }
    outb(0x60, value);
    return 1;
}

static int mouse_read_data(unsigned char* out) {
    if (!out || !mouse_wait_output_full()) {
        return 0;
    }
    *out = inb(0x60);
    return 1;
}

static int mouse_expect_ack(void) {
    unsigned char data = 0;
    if (!mouse_read_data(&data)) {
        return 0;
    }
    return data == 0xFA;
}

static void mouse_apply_packet(void) {
    int dx;
    int dy;
    int width;
    int height;

    if ((g_packet[0] & 0xC0) != 0) {
        return;
    }

    dx = (g_packet[0] & 0x10) ? ((int)g_packet[1] - 256) : (int)g_packet[1];
    dy = (g_packet[0] & 0x20) ? ((int)g_packet[2] - 256) : (int)g_packet[2];

    width = video_get_width();
    height = video_get_height();
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }

    g_mouse_state.dx = dx;
    g_mouse_state.dy = -dy;
    g_mouse_state.x += dx;
    g_mouse_state.y -= dy;

    if (g_mouse_state.x < 0) {
        g_mouse_state.x = 0;
    } else if (g_mouse_state.x >= width) {
        g_mouse_state.x = width - 1;
    }

    if (g_mouse_state.y < 0) {
        g_mouse_state.y = 0;
    } else if (g_mouse_state.y >= height) {
        g_mouse_state.y = height - 1;
    }

    g_mouse_state.buttons = (unsigned int)(g_packet[0] & 0x07);
    g_mouse_state.seq++;
    if (!g_packet_logged) {
        log_serial_raw("[mouse] first packet received\n");
        g_packet_logged = 1;
    }
}

void mouse_init(void) {
    unsigned char command_byte;
    int width;
    int height;

    g_mouse_state.x = 0;
    g_mouse_state.y = 0;
    g_mouse_state.dx = 0;
    g_mouse_state.dy = 0;
    g_mouse_state.buttons = 0;
    g_mouse_state.seq = 0;
    g_mouse_state.present = 0;
    g_packet_index = 0;
    g_packet_logged = 0;

    width = video_get_width();
    height = video_get_height();
    g_mouse_state.x = width / 2;
    g_mouse_state.y = height / 2;

    mouse_drain_output();

    if (!mouse_wait_input_empty()) {
        log_serial_raw("[mouse] controller busy, mouse disabled\n");
        return;
    }

    outb(0x64, 0xA8);
    command_byte = mouse_read_controller_command_byte();
    command_byte |= 0x02;
    command_byte &= (unsigned char)~0x20U;
    if (!mouse_write_controller_command_byte(command_byte)) {
        log_serial_raw("[mouse] command byte update failed\n");
        return;
    }

    mouse_drain_output();

    if (!mouse_write_device(0xF6) || !mouse_expect_ack()) {
        log_serial_raw("[mouse] default-settings ACK missing\n");
        return;
    }

    if (!mouse_write_device(0xF4) || !mouse_expect_ack()) {
        log_serial_raw("[mouse] enable-data ACK missing\n");
        return;
    }

    g_mouse_state.present = 1;
    log_serial_raw("[mouse] PS/2 mouse enabled on IRQ12\n");
}

void mouse_handle_irq(void) {
    unsigned char status = inb(0x64);
    unsigned char data;

    if ((status & 0x01) == 0) {
        return;
    }

    data = inb(0x60);
    if ((status & 0x20) == 0) {
        return;
    }

    if (!g_mouse_state.present) {
        return;
    }

    if (g_packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    g_packet[g_packet_index++] = data;
    if (g_packet_index == 3) {
        g_packet_index = 0;
        mouse_apply_packet();
    }
}

int mouse_get_state(mouse_state_t* out) {
    if (!out) {
        return 0;
    }
    *out = g_mouse_state;
    return 1;
}

#include "timer.h"
#include "logger.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_HZ 1193182U
#define EFLAGS_IF 0x00000200U

static volatile unsigned int g_timer_ticks = 0;
static volatile unsigned int g_timer_hz = 0;
static volatile int g_timer_ready = 0;

static inline void outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned int read_eflags(void) {
    unsigned int flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

void timer_init(unsigned int hz) {
    unsigned int divisor;
    unsigned int actual_hz;

    if (hz == 0) {
        hz = 100;
    }

    divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFU) {
        divisor = 0xFFFFU;
    }

    actual_hz = PIT_BASE_HZ / divisor;
    if (actual_hz == 0) {
        actual_hz = 1;
    }

    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (unsigned char)((divisor >> 8) & 0xFF));

    g_timer_ticks = 0;
    g_timer_hz = actual_hz;
    g_timer_ready = 1;

    log_serial_raw("[timer] PIT configured on IRQ0\n");
}

void timer_on_tick(void) {
    if (g_timer_ready) {
        g_timer_ticks++;
    }
}

unsigned int timer_get_ticks(void) {
    return g_timer_ticks;
}

unsigned int timer_get_hz(void) {
    return g_timer_hz;
}

int timer_is_ready(void) {
    return g_timer_ready;
}

unsigned int timer_ms_to_ticks_ceil(unsigned int ms) {
    unsigned int hz;
    unsigned int seconds;
    unsigned int remainder_ms;
    unsigned int ticks;
    unsigned int remainder_ticks;

    if (ms == 0) {
        return 0;
    }

    hz = g_timer_hz;
    if (!g_timer_ready || hz == 0) {
        return 0;
    }

    seconds = ms / 1000U;
    remainder_ms = ms % 1000U;

    if (seconds > 0 && hz > 0xFFFFFFFFU / seconds) {
        return 0xFFFFFFFFU;
    }

    ticks = seconds * hz;
    remainder_ticks = (remainder_ms * hz + 999U) / 1000U;

    if (ticks > 0xFFFFFFFFU - remainder_ticks) {
        return 0xFFFFFFFFU;
    }

    ticks += remainder_ticks;
    if (ticks == 0) {
        ticks = 1;
    }

    return ticks;
}

void timer_wait_for_interrupt(void) {
    if (!g_timer_ready || (read_eflags() & EFLAGS_IF) == 0U) {
        return;
    }

    __asm__ volatile ("hlt");
}

void timer_sleep_ticks(unsigned int ticks) {
    unsigned int start;

    if (ticks == 0 || !g_timer_ready || (read_eflags() & EFLAGS_IF) == 0U) {
        return;
    }

    start = g_timer_ticks;
    while ((unsigned int)(g_timer_ticks - start) < ticks) {
        timer_wait_for_interrupt();
    }
}

void timer_sleep_ms(unsigned int ms) {
    timer_sleep_ticks(timer_ms_to_ticks_ceil(ms));
}

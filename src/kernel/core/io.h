#ifndef IO_H
#define IO_H

/*
 * Shared x86 port I/O primitives.
 *
 * Historically every subsystem that touched hardware ports re-declared its own
 * file-local `inb`/`outb`. That duplication is the target of docs/TODO.md
 * section 2 ("Consolidate low-level helpers (inb/outb, ...) into shared
 * infrastructure"). New code MUST include this header instead of redefining the
 * primitives; existing files are migrated incrementally.
 */

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short inw(unsigned short port) {
    unsigned short val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Short, port-mapped delay used after slow ISA writes (e.g. PIC, CMOS). */
static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

#endif /* IO_H */

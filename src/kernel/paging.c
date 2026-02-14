#include "paging.h"
#include "serial.h"
#include "logger.h"

#define PAGE_PRESENT 0x001
#define PAGE_RW      0x002
#define BOOT_VIDEO_FB_ADDR 0x0520

struct idt_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static unsigned int page_directory[1024] __attribute__((aligned(4096)));
static unsigned int first_page_table[1024] __attribute__((aligned(4096)));
static unsigned int fb_page_table[1024] __attribute__((aligned(4096)));
static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;

static inline unsigned int read_cr2(void) {
    unsigned int value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline unsigned int read_phys_u32(unsigned int addr) {
    unsigned int value;
    __asm__ volatile ("movl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static inline void load_idt(struct idt_ptr* ptr) {
    __asm__ volatile ("lidtl (%0)" : : "r"(ptr));
}

static inline void load_page_directory(unsigned int* pd) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pd) : "memory");
}

static inline void enable_paging(void) {
    unsigned int cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static void idt_set_gate(int index, unsigned int handler, unsigned short selector, unsigned char flags) {
    idt[index].offset_low = (unsigned short)(handler & 0xFFFF);
    idt[index].selector = selector;
    idt[index].zero = 0;
    idt[index].type_attr = flags;
    idt[index].offset_high = (unsigned short)((handler >> 16) & 0xFFFF);
}

static void panic_halt(void) {
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((used)) static void page_fault_handler_c(unsigned int error_code, unsigned int eip) {
    unsigned int cr2 = read_cr2();

    log_serial_raw("[paging] #PF detected\n");
    log_serial_raw("[paging] CR2=");
    serial_print_hex(cr2);
    log_serial_raw(" error=");
    serial_print_hex(error_code);
    log_serial_raw(" eip=");
    serial_print_hex(eip);
    log_serial_raw("\n");
    log_serial_raw("[paging] panic halt\n");

    panic_halt();
}

__attribute__((used, naked)) static void page_fault_isr_stub(void) {
    __asm__ volatile (
        "pusha\n"
        "push %ds\n"
        "push %es\n"
        "push %fs\n"
        "push %gs\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "mov 52(%esp), %eax\n"
        "push %eax\n"
        "mov 52(%esp), %eax\n"
        "push %eax\n"
        "call page_fault_handler_c\n"
        "add $8, %esp\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "add $4, %esp\n"
        "iret\n"
    );
}

static void paging_setup_structures(void) {
    unsigned int fb_addr;

    for (int i = 0; i < 1024; i++) {
        first_page_table[i] = (i * 0x1000) | PAGE_PRESENT | PAGE_RW;
        fb_page_table[i] = 0;
        page_directory[i] = 0;
    }

    page_directory[0] = ((unsigned int)first_page_table) | PAGE_PRESENT | PAGE_RW;

    fb_addr = read_phys_u32(BOOT_VIDEO_FB_ADDR);
    if (fb_addr >= 0x00400000) {
        unsigned int fb_base = fb_addr & 0xFFC00000;
        unsigned int pd_index = fb_base >> 22;

        for (int i = 0; i < 1024; i++) {
            fb_page_table[i] = (fb_base + (i * 0x1000)) | PAGE_PRESENT | PAGE_RW;
        }
        page_directory[pd_index] = ((unsigned int)fb_page_table) | PAGE_PRESENT | PAGE_RW;
    }
}

static void paging_setup_idt(void) {
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    idt_set_gate(14, (unsigned int)page_fault_isr_stub, 0x08, 0x8E);

    idtp.limit = (unsigned short)(sizeof(idt) - 1);
    idtp.base = (unsigned int)&idt;
    load_idt(&idtp);
}

static void paging_self_test(void) {
    volatile unsigned short* text_vram = (volatile unsigned short*)0xB8000;
    unsigned int stack_ptr;
    unsigned short boot_meta_value;

    __asm__ volatile ("mov %%esp, %0" : "=r"(stack_ptr));
    __asm__ volatile ("movw (0x500), %0" : "=r"(boot_meta_value));

    (void)boot_meta_value;
    unsigned short cell = *text_vram;
    *text_vram = cell;
    volatile unsigned int* stack_probe = (volatile unsigned int*)(stack_ptr & ~0x3U);
    (void)(*stack_probe);

    log_serial_raw("paging self-test OK\n");
}

void paging_init(void) {
    log_serial_raw("[paging] init\n");

    paging_setup_structures();
    paging_setup_idt();

    log_serial_raw("[paging] loading CR3\n");
    load_page_directory(page_directory);

    log_serial_raw("[paging] enabling CR0.PG\n");
    enable_paging();
    log_serial_raw("[paging] enabled\n");

    paging_self_test();

#ifdef PAGING_TEST_PF
    log_serial_raw("[paging] triggering #PF test\n");
    {
        volatile unsigned int* bad = (volatile unsigned int*)0x500000;
        volatile unsigned int value = *bad;
        (void)value;
    }
#endif
}

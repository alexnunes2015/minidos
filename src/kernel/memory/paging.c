#include "paging.h"
#include "serial.h"
#include "logger.h"
#include "video.h"
#include "keyboard.h"
#include "mouse.h"
#include "scheduler.h"
#include "timer.h"

#define PAGE_PRESENT 0x001
#define PAGE_RW      0x002
#define PAGE_PWT     0x008
#define PAGE_PCD     0x010
#define LOWMEM_PAGE_TABLE_COUNT 3
#define PAGING_TEST_FAULT_ADDR 0x00C00000
#define BOOT_VIDEO_FLAG_ADDR 0x0510
#define BOOT_VIDEO_HEIGHT_ADDR 0x0514
#define BOOT_VIDEO_PITCH_ADDR 0x0516
#define BOOT_VIDEO_FB_ADDR 0x0520
#define FB_PAGE_TABLE_COUNT 2
#define STOP_PAGE_FAULT "STOP 0x0000000E"
#define STOP_KERNEL_EXCEPTION "STOP 0x00000005"
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

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
static unsigned int lowmem_page_tables[LOWMEM_PAGE_TABLE_COUNT][1024] __attribute__((aligned(4096)));
static unsigned int fb_page_tables[FB_PAGE_TABLE_COUNT][1024] __attribute__((aligned(4096)));
static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idtp;
static int interrupt_handlers_ready = 0;
static int page_fault_in_progress = 0;

static inline unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned int read_cr2(void) {
    unsigned int value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline void invlpg_addr(unsigned int addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline unsigned int read_phys_u32(unsigned int addr) {
    unsigned int value;
    __asm__ volatile ("movl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static inline unsigned short read_phys_u16(unsigned int addr) {
    unsigned short value;
    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static inline unsigned char read_phys_u8(unsigned int addr) {
    unsigned char value;
    __asm__ volatile ("movb (%1), %0" : "=r"(value) : "r"(addr) : "memory");
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

static void pic_send_eoi(unsigned int irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

static void pic_remap(void) {
    unsigned char master_mask = inb(PIC1_DATA);
    unsigned char slave_mask = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

static void pic_set_mask(unsigned char master_mask, unsigned char slave_mask) {
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

static void bsod_wait_key_then_reboot(void) {
    timer_sleep_ms(1000);

    while (serial_received()) {
        (void)serial_getchar();
    }
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }

    while (1) {
        if (serial_received()) {
            (void)serial_getchar();
            break;
        }
        if (inb(0x64) & 0x01) {
            (void)inb(0x60);
            break;
        }
        timer_wait_for_interrupt();
    }

    outb(0x64, 0xFE);
    panic_halt();
}

__attribute__((used)) static void page_fault_handler_c(unsigned int error_code, unsigned int eip) {
    unsigned int cr2 = read_cr2();
    int guard_pid = -1;
    const char* guard_name = 0;

    if (page_fault_in_progress) {
        log_serial_raw("[paging] nested #PF halt\n");
        panic_halt();
    }
    page_fault_in_progress = 1;

    if (scheduler_describe_guard_fault(cr2, &guard_pid, &guard_name)) {
        log_serial_raw("SCHED900 guard-page pid=");
        serial_print_hex((unsigned int)guard_pid);
        log_serial_raw(" name=");
        log_serial_raw(guard_name ? guard_name : "unknown");
        log_serial_raw("\n");
    }

    log_serial_raw("[paging] #PF detected\n");
    log_serial_raw("[paging] CR2=");
    serial_print_hex(cr2);
    log_serial_raw(" error=");
    serial_print_hex(error_code);
    log_serial_raw(" eip=");
    serial_print_hex(eip);
    log_serial_raw("\n");
    video_show_bsod(STOP_PAGE_FAULT, "A PAGE FAULT OCCURRED DURING BOOT.");
    log_serial_raw("[paging] waiting key for reboot\n");
    bsod_wait_key_then_reboot();
}

static void exception_panic(const irq_frame_t* frame) {
    static const char* exception_names[32] = {
        "Divide Error", "Debug", "NMI", "Breakpoint",
        "Overflow", "BOUND Range", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating-Point", "Alignment Check", "Machine Check", "SIMD Floating-Point",
        "Virtualization", "Control Protection", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection", "VMM Communication", "Security", "Reserved"
    };

    unsigned int vector = frame->vector;

    log_serial_raw("[int] exception ");
    serial_print_hex(vector);
    log_serial_raw(" (");
    if (vector < 32) {
        log_serial_raw(exception_names[vector]);
    } else {
        log_serial_raw("unknown");
    }
    log_serial_raw(")\n");
    log_serial_raw("[int] err=");
    serial_print_hex(frame->error_code);
    log_serial_raw(" eip=");
    serial_print_hex(frame->eip);
    log_serial_raw(" cs=");
    serial_print_hex(frame->cs);
    log_serial_raw(" eflags=");
    serial_print_hex(frame->eflags);
    log_serial_raw("\n");

    video_show_bsod(STOP_KERNEL_EXCEPTION, "CPU EXCEPTION IN KERNEL MODE.");
    bsod_wait_key_then_reboot();
}

__attribute__((used)) static irq_frame_t* interrupt_dispatch_c(irq_frame_t* frame) {
    if (!frame) {
        panic_halt();
    }

    if (frame->vector < 32) {
        if (frame->vector == 14) {
            page_fault_handler_c(frame->error_code, frame->eip);
            return 0;
        }
        exception_panic(frame);
        return 0;
    }

    if (frame->vector == 33) {
        keyboard_handle_irq();
        pic_send_eoi(1);
        return 0;
    }

    if (frame->vector == 44) {
        mouse_handle_irq();
        pic_send_eoi(12);
        return 0;
    }

    if (frame->vector == 32) {
        irq_frame_t* next_frame = scheduler_on_timer_tick(frame);
        pic_send_eoi(0);
        return next_frame;
    }

    if (frame->vector == 129) {
        return scheduler_on_yield(frame);
    }

    if (frame->vector >= 32 && frame->vector <= 47) {
        pic_send_eoi(frame->vector - 32);
    }

    return 0;
}

__attribute__((used, naked)) static void interrupt_common_stub(void) {
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
        "push %esp\n"
        "call interrupt_dispatch_c\n"
        "add $4, %esp\n"
        "test %eax, %eax\n"
        "jz 1f\n"
        "mov %eax, %esp\n"
        "1:\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "add $8, %esp\n"
        "iret\n"
    );
}

#define ISR_NOERR_STUB(name, vector) \
    __attribute__((used, naked)) static void name(void) { \
        __asm__ volatile ("pushl $0\npushl $" #vector "\njmp interrupt_common_stub\n"); \
    }

#define ISR_ERR_STUB(name, vector) \
    __attribute__((used, naked)) static void name(void) { \
        __asm__ volatile ("pushl $" #vector "\njmp interrupt_common_stub\n"); \
    }

ISR_NOERR_STUB(isr0, 0)
ISR_NOERR_STUB(isr1, 1)
ISR_NOERR_STUB(isr2, 2)
ISR_NOERR_STUB(isr3, 3)
ISR_NOERR_STUB(isr4, 4)
ISR_NOERR_STUB(isr5, 5)
ISR_NOERR_STUB(isr6, 6)
ISR_NOERR_STUB(isr7, 7)
ISR_ERR_STUB(isr8, 8)
ISR_NOERR_STUB(isr9, 9)
ISR_ERR_STUB(isr10, 10)
ISR_ERR_STUB(isr11, 11)
ISR_ERR_STUB(isr12, 12)
ISR_ERR_STUB(isr13, 13)
ISR_ERR_STUB(isr14, 14)
ISR_NOERR_STUB(isr15, 15)
ISR_NOERR_STUB(isr16, 16)
ISR_ERR_STUB(isr17, 17)
ISR_NOERR_STUB(isr18, 18)
ISR_NOERR_STUB(isr19, 19)
ISR_NOERR_STUB(isr20, 20)
ISR_ERR_STUB(isr21, 21)
ISR_NOERR_STUB(isr22, 22)
ISR_NOERR_STUB(isr23, 23)
ISR_NOERR_STUB(isr24, 24)
ISR_NOERR_STUB(isr25, 25)
ISR_NOERR_STUB(isr26, 26)
ISR_NOERR_STUB(isr27, 27)
ISR_NOERR_STUB(isr28, 28)
ISR_NOERR_STUB(isr29, 29)
ISR_ERR_STUB(isr30, 30)
ISR_NOERR_STUB(isr31, 31)

ISR_NOERR_STUB(irq0, 32)
ISR_NOERR_STUB(irq1, 33)
ISR_NOERR_STUB(irq2, 34)
ISR_NOERR_STUB(irq3, 35)
ISR_NOERR_STUB(irq4, 36)
ISR_NOERR_STUB(irq5, 37)
ISR_NOERR_STUB(irq6, 38)
ISR_NOERR_STUB(irq7, 39)
ISR_NOERR_STUB(irq8, 40)
ISR_NOERR_STUB(irq9, 41)
ISR_NOERR_STUB(irq10, 42)
ISR_NOERR_STUB(irq11, 43)
ISR_NOERR_STUB(irq12, 44)
ISR_NOERR_STUB(irq13, 45)
ISR_NOERR_STUB(irq14, 46)
ISR_NOERR_STUB(irq15, 47)
ISR_NOERR_STUB(isr129, 129)

static void paging_setup_structures(void) {
    unsigned int fb_addr;
    unsigned int fb_size = 0;

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }
    for (int table = 0; table < LOWMEM_PAGE_TABLE_COUNT; table++) {
        unsigned int region_base = table << 22;

        for (int i = 0; i < 1024; i++) {
            lowmem_page_tables[table][i] = (region_base + (i * 0x1000)) | PAGE_PRESENT | PAGE_RW;
        }
        page_directory[table] = ((unsigned int)lowmem_page_tables[table]) | PAGE_PRESENT | PAGE_RW;
    }
    for (int table = 0; table < FB_PAGE_TABLE_COUNT; table++) {
        for (int i = 0; i < 1024; i++) {
            fb_page_tables[table][i] = 0;
        }
    }

    fb_addr = read_phys_u32(BOOT_VIDEO_FB_ADDR);
    if (read_phys_u8(BOOT_VIDEO_FLAG_ADDR) == 1) {
        unsigned int height = read_phys_u16(BOOT_VIDEO_HEIGHT_ADDR);
        unsigned int pitch = read_phys_u16(BOOT_VIDEO_PITCH_ADDR);

        if (height != 0 && pitch != 0) {
            fb_size = height * pitch;
        }
    }

    if (fb_addr != 0 && fb_size != 0) {
        unsigned int fb_end = fb_addr + fb_size - 1;
        unsigned int start_pd = fb_addr >> 22;
        unsigned int end_pd = fb_end >> 22;
        int table = 0;

        log_serial_raw("[paging] fbmap addr=");
        serial_print_hex(fb_addr);
        log_serial_raw(" size=");
        serial_print_hex(fb_size);
        log_serial_raw(" start_pd=");
        serial_print_hex(start_pd);
        log_serial_raw(" end_pd=");
        serial_print_hex(end_pd);
        log_serial_raw("\n");

        for (unsigned int pd_index = start_pd; pd_index <= end_pd; pd_index++) {
            unsigned int region_base;

            if (pd_index == 0) {
                continue;
            }
            if (table >= FB_PAGE_TABLE_COUNT) {
                break;
            }

            region_base = pd_index << 22;
            for (int i = 0; i < 1024; i++) {
                fb_page_tables[table][i] = (region_base + (i * 0x1000)) | PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PCD;
            }
            page_directory[pd_index] = ((unsigned int)fb_page_tables[table]) | PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PCD;
            table++;
        }
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

    idt_set_gate(0, (unsigned int)isr0, 0x08, 0x8E);
    idt_set_gate(1, (unsigned int)isr1, 0x08, 0x8E);
    idt_set_gate(2, (unsigned int)isr2, 0x08, 0x8E);
    idt_set_gate(3, (unsigned int)isr3, 0x08, 0x8E);
    idt_set_gate(4, (unsigned int)isr4, 0x08, 0x8E);
    idt_set_gate(5, (unsigned int)isr5, 0x08, 0x8E);
    idt_set_gate(6, (unsigned int)isr6, 0x08, 0x8E);
    idt_set_gate(7, (unsigned int)isr7, 0x08, 0x8E);
    idt_set_gate(8, (unsigned int)isr8, 0x08, 0x8E);
    idt_set_gate(9, (unsigned int)isr9, 0x08, 0x8E);
    idt_set_gate(10, (unsigned int)isr10, 0x08, 0x8E);
    idt_set_gate(11, (unsigned int)isr11, 0x08, 0x8E);
    idt_set_gate(12, (unsigned int)isr12, 0x08, 0x8E);
    idt_set_gate(13, (unsigned int)isr13, 0x08, 0x8E);
    idt_set_gate(14, (unsigned int)isr14, 0x08, 0x8E);
    idt_set_gate(15, (unsigned int)isr15, 0x08, 0x8E);
    idt_set_gate(16, (unsigned int)isr16, 0x08, 0x8E);
    idt_set_gate(17, (unsigned int)isr17, 0x08, 0x8E);
    idt_set_gate(18, (unsigned int)isr18, 0x08, 0x8E);
    idt_set_gate(19, (unsigned int)isr19, 0x08, 0x8E);
    idt_set_gate(20, (unsigned int)isr20, 0x08, 0x8E);
    idt_set_gate(21, (unsigned int)isr21, 0x08, 0x8E);
    idt_set_gate(22, (unsigned int)isr22, 0x08, 0x8E);
    idt_set_gate(23, (unsigned int)isr23, 0x08, 0x8E);
    idt_set_gate(24, (unsigned int)isr24, 0x08, 0x8E);
    idt_set_gate(25, (unsigned int)isr25, 0x08, 0x8E);
    idt_set_gate(26, (unsigned int)isr26, 0x08, 0x8E);
    idt_set_gate(27, (unsigned int)isr27, 0x08, 0x8E);
    idt_set_gate(28, (unsigned int)isr28, 0x08, 0x8E);
    idt_set_gate(29, (unsigned int)isr29, 0x08, 0x8E);
    idt_set_gate(30, (unsigned int)isr30, 0x08, 0x8E);
    idt_set_gate(31, (unsigned int)isr31, 0x08, 0x8E);

    idt_set_gate(32, (unsigned int)irq0, 0x08, 0x8E);
    idt_set_gate(33, (unsigned int)irq1, 0x08, 0x8E);
    idt_set_gate(34, (unsigned int)irq2, 0x08, 0x8E);
    idt_set_gate(35, (unsigned int)irq3, 0x08, 0x8E);
    idt_set_gate(36, (unsigned int)irq4, 0x08, 0x8E);
    idt_set_gate(37, (unsigned int)irq5, 0x08, 0x8E);
    idt_set_gate(38, (unsigned int)irq6, 0x08, 0x8E);
    idt_set_gate(39, (unsigned int)irq7, 0x08, 0x8E);
    idt_set_gate(40, (unsigned int)irq8, 0x08, 0x8E);
    idt_set_gate(41, (unsigned int)irq9, 0x08, 0x8E);
    idt_set_gate(42, (unsigned int)irq10, 0x08, 0x8E);
    idt_set_gate(43, (unsigned int)irq11, 0x08, 0x8E);
    idt_set_gate(44, (unsigned int)irq12, 0x08, 0x8E);
    idt_set_gate(45, (unsigned int)irq13, 0x08, 0x8E);
    idt_set_gate(46, (unsigned int)irq14, 0x08, 0x8E);
    idt_set_gate(47, (unsigned int)irq15, 0x08, 0x8E);
    idt_set_gate(129, (unsigned int)isr129, 0x08, 0x8E);

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

int paging_init(void) {
    log_serial_raw("[paging] init\n");

    paging_setup_structures();
    paging_setup_idt();

    if (page_directory[0] == 0 || idt[14].selector == 0) {
        log_serial_raw("[paging] setup validation failed\n");
        return -1;
    }

    log_serial_raw("[paging] loading CR3\n");
    load_page_directory(page_directory);

    log_serial_raw("[paging] enabling CR0.PG\n");
    enable_paging();
    log_serial_raw("[paging] enabled\n");

    paging_self_test();

#ifdef PAGING_TEST_PF
    log_serial_raw("[paging] triggering #PF test\n");
    {
        volatile unsigned int* bad = (volatile unsigned int*)PAGING_TEST_FAULT_ADDR;
        volatile unsigned int value = *bad;
        (void)value;
    }
#endif

    return 0;
}

static unsigned int* paging_lookup_pte(unsigned int addr) {
    unsigned int pd_index = addr >> 22;
    unsigned int pt_index = (addr >> 12) & 0x3FFU;
    unsigned int entry = page_directory[pd_index];
    unsigned int* page_table;

    if ((entry & PAGE_PRESENT) == 0) {
        return 0;
    }

    page_table = (unsigned int*)(entry & ~0xFFFU);
    return &page_table[pt_index];
}

int paging_page_present(unsigned int addr) {
    unsigned int* pte = paging_lookup_pte(addr);

    if (!pte) {
        return 0;
    }

    return (*pte & PAGE_PRESENT) != 0;
}

int paging_unmap_page(unsigned int addr) {
    unsigned int* pte = paging_lookup_pte(addr);

    if (!pte) {
        return 0;
    }

    *pte = 0;
    invlpg_addr(addr);
    return 1;
}

unsigned int* paging_get_kernel_directory(void) {
    return page_directory;
}

void paging_activate_directory(unsigned int* pd) {
    if (!pd) {
        pd = page_directory;
    }
    load_page_directory(pd);
}

int paging_build_app_directory(unsigned int* out_pd, unsigned int* out_lowmem_pt0, unsigned int app_phys_base, unsigned int app_bytes) {
    unsigned int first_index = 0x200000U >> 12;
    unsigned int page_count = (app_bytes + 0xFFFU) >> 12;

    if (!out_pd || !out_lowmem_pt0 || page_count == 0 || page_count > 256U) {
        return 0;
    }

    for (int i = 0; i < 1024; i++) {
        out_pd[i] = page_directory[i];
        out_lowmem_pt0[i] = lowmem_page_tables[0][i];
    }

    for (unsigned int i = 0; i < page_count; i++) {
        out_lowmem_pt0[first_index + i] = (app_phys_base + (i * 0x1000U)) | PAGE_PRESENT | PAGE_RW;
    }

    out_pd[0] = ((unsigned int)out_lowmem_pt0) | PAGE_PRESENT | PAGE_RW;
    return 1;
}

void interrupts_init(void) {
    if (interrupt_handlers_ready) {
        return;
    }

    keyboard_init();
    mouse_init();
    keyboard_set_irq_mode(1);
    scheduler_init_timer(100);
    pic_remap();
    pic_set_mask(0xF8, 0xEF);
    interrupt_handlers_ready = 1;
    log_serial_raw("[int] IDT active, PIC remapped, IRQ0/IRQ1/IRQ12 enabled\n");
    __asm__ volatile ("sti");
}

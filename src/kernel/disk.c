#include "disk.h"

// ATA PIO ports
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_DATA        0
#define ATA_ERROR       1
#define ATA_FEATURES    1
#define ATA_SECCOUNT    2
#define ATA_LBALO       3
#define ATA_LBAMID      4
#define ATA_LBAHI       5
#define ATA_DRIVE       6
#define ATA_COMMAND     7
#define ATA_STATUS      7

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01
#define ATA_DCR_SRST    0x04

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void insl(unsigned short port, unsigned int* buffer, unsigned int count) {
    __asm__ volatile ("cld; rep insl" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
}

static inline void insw(unsigned short port, unsigned short* buffer, unsigned int count) {
    __asm__ volatile ("cld; rep insw" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(unsigned short port, const unsigned short* buffer, unsigned int count) {
    __asm__ volatile ("cld; rep outsw" : "+S"(buffer), "+c"(count) : "d"(port));
}

static inline void io_wait() {
    outb(0x80, 0);
}

typedef struct {
    unsigned short io_base;
    unsigned short ctrl_base;
    unsigned char drive_sel;
} ata_target_t;

static int ata_decode_target(unsigned char disk_id, ata_target_t* target) {
    if (!target || disk_id > 3) {
        return -1;
    }

    target->io_base = (disk_id < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    target->ctrl_base = (disk_id < 2) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
    target->drive_sel = (disk_id & 1) ? 0x10 : 0x00; // Slave bit
    return 0;
}

static int ata_has_error(const ata_target_t* target) {
    return (inb(target->io_base + ATA_STATUS) & ATA_SR_ERR) != 0;
}

static int ata_wait_bsy(const ata_target_t* target) {
    unsigned int timeout = 1000000;
    while ((inb(target->io_base + ATA_STATUS) & ATA_SR_BSY) && timeout > 0) {
        timeout--;
    }
    return timeout > 0 ? 0 : -1;
}

static int ata_wait_drdy(const ata_target_t* target) {
    unsigned int timeout = 1000000;
    while (!(inb(target->io_base + ATA_STATUS) & ATA_SR_DRDY) && timeout > 0) {
        timeout--;
    }
    return timeout > 0 ? 0 : -1;
}

static int ata_wait_drq(const ata_target_t* target) {
    unsigned int timeout = 1000000;
    while (timeout > 0) {
        unsigned char status = inb(target->io_base + ATA_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
        timeout--;
    }
    return -1;
}

static void ata_soft_reset(const ata_target_t* target) {
    if (!target) return;
    outb(target->ctrl_base, ATA_DCR_SRST);
    io_wait();
    io_wait();
    io_wait();
    io_wait();
    outb(target->ctrl_base, 0x00);
    io_wait();
    io_wait();
    io_wait();
    io_wait();
}

static int disk_read_lba_internal(const ata_target_t* target, unsigned int lba, unsigned char* buffer) {
    if (!buffer || !target) {
        return -1;
    }

    // Select target drive first; previous probe may have left another device selected.
    outb(target->io_base + ATA_DRIVE, (unsigned char)(0xE0 | target->drive_sel | ((lba >> 24) & 0x0F)));
    io_wait();
    io_wait();
    io_wait();
    io_wait();

    if (ata_wait_bsy(target) != 0) {
        return -1;
    }
    if (ata_wait_drdy(target) != 0) {
        return -1;
    }

    outb(target->io_base + ATA_SECCOUNT, 1);
    outb(target->io_base + ATA_LBALO, (unsigned char)(lba & 0xFF));
    outb(target->io_base + ATA_LBAMID, (unsigned char)((lba >> 8) & 0xFF));
    outb(target->io_base + ATA_LBAHI, (unsigned char)((lba >> 16) & 0xFF));
    outb(target->io_base + ATA_COMMAND, ATA_CMD_READ_PIO);
    io_wait();

    if (ata_wait_drq(target) != 0) {
        return -1;
    }
    if (ata_has_error(target)) {
        return -1;
    }

    insw(target->io_base + ATA_DATA, (unsigned short*)buffer, 256);
    return 0;
}

static int disk_write_lba_internal(const ata_target_t* target, unsigned int lba, unsigned char* buffer) {
    if (!buffer || !target) {
        return -1;
    }

    // Select target drive first; previous probe may have left another device selected.
    outb(target->io_base + ATA_DRIVE, (unsigned char)(0xE0 | target->drive_sel | ((lba >> 24) & 0x0F)));
    io_wait();
    io_wait();
    io_wait();
    io_wait();

    if (ata_wait_bsy(target) != 0) {
        return -1;
    }
    if (ata_wait_drdy(target) != 0) {
        return -1;
    }

    outb(target->io_base + ATA_SECCOUNT, 1);
    outb(target->io_base + ATA_LBALO, (unsigned char)(lba & 0xFF));
    outb(target->io_base + ATA_LBAMID, (unsigned char)((lba >> 8) & 0xFF));
    outb(target->io_base + ATA_LBAHI, (unsigned char)((lba >> 16) & 0xFF));
    outb(target->io_base + ATA_COMMAND, ATA_CMD_WRITE_PIO);
    io_wait();

    if (ata_wait_drq(target) != 0) {
        return -1;
    }
    if (ata_has_error(target)) {
        return -1;
    }

    outsw(target->io_base + ATA_DATA, (const unsigned short*)buffer, 256);
    outb(target->io_base + ATA_COMMAND, ATA_CMD_CACHE_FLUSH);

    if (ata_wait_bsy(target) != 0) {
        return -1;
    }
    if (ata_wait_drdy(target) != 0) {
        return -1;
    }
    if (ata_has_error(target)) {
        return -1;
    }

    return 0;
}

void disk_init() {
    ata_target_t primary_master;
    ata_target_t secondary_master;

    if (ata_decode_target(0, &primary_master) == 0) {
        outb(primary_master.io_base + ATA_DRIVE, 0xE0);
        io_wait();
    }
    if (ata_decode_target(2, &secondary_master) == 0) {
        outb(secondary_master.io_base + ATA_DRIVE, 0xE0);
        io_wait();
    }
}

int disk_read_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer) {
    ata_target_t target;
    if (ata_decode_target(disk_id, &target) != 0) {
        return -1;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (disk_read_lba_internal(&target, lba, buffer) == 0) {
            return 0;
        }
        ata_soft_reset(&target);
    }
    return -1;
}

int disk_write_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer) {
    ata_target_t target;
    if (ata_decode_target(disk_id, &target) != 0) {
        return -1;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (disk_write_lba_internal(&target, lba, buffer) == 0) {
            return 0;
        }
        ata_soft_reset(&target);
    }
    return -1;
}

int disk_read_lba(unsigned int lba, unsigned char* buffer) {
    return disk_read_lba_from_disk(0, lba, buffer);
}

int disk_write_lba(unsigned int lba, unsigned char* buffer) {
    return disk_write_lba_from_disk(0, lba, buffer);
}

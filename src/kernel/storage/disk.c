#include "disk.h"
#include "boot_splash.h"
#include "logger.h"
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
#define ATA_CMD_IDENTIFY    0xEC

#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01
#define ATA_DCR_SRST    0x04

#define BOOT_DRIVE_NUMBER_ADDR   0x0504
#define BOOT_DRIVE_FLAGS_ADDR    0x0505
#define BOOT_DRIVE_SPT_ADDR      0x0506
#define BOOT_DRIVE_HEADS_ADDR    0x0508
#define BOOT_GEOMETRY_VALID_FLAG 0x02
#define BIOS_SECTOR_SIZE         512
#define ATA_WAIT_SPLASH_POLL_MASK 0x0FFFu

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

static inline unsigned char read_phys_u8(unsigned int addr) {
    unsigned char value;
    __asm__ volatile ("movb (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static inline unsigned short read_phys_u16(unsigned int addr) {
    unsigned short value;
    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static inline unsigned short read_le16(const unsigned char* ptr) {
    return (unsigned short)(ptr[0] | (ptr[1] << 8));
}

static inline unsigned int read_le32(const unsigned char* ptr) {
    return (unsigned int)(ptr[0] |
                          (ptr[1] << 8) |
                          (ptr[2] << 16) |
                          (ptr[3] << 24));
}

extern int biosdisk_boot_read_sector(unsigned int drive, unsigned int cylinder, unsigned int head, unsigned int sector);
extern int biosdisk_boot_write_sector(unsigned int drive, unsigned int cylinder, unsigned int head, unsigned int sector);
extern unsigned char biosdisk_transfer_buffer[BIOS_SECTOR_SIZE];

typedef struct {
    unsigned short io_base;
    unsigned short ctrl_base;
    unsigned char drive_sel;
} ata_target_t;

typedef struct {
    unsigned char drive_number;
    unsigned short sectors_per_track;
    unsigned short heads;
    unsigned int total_sectors;
    int is_floppy;
    int geometry_valid;
} boot_media_t;

static boot_media_t boot_media;
static unsigned char ata_presence_mask;
static unsigned short ata_identify_buffer[256];

static int ata_decode_physical_target(unsigned char ata_id, ata_target_t* target) {
    if (!target || ata_id > 3) {
        return -1;
    }

    target->io_base = (ata_id < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    target->ctrl_base = (ata_id < 2) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
    target->drive_sel = (ata_id & 1) ? 0x10 : 0x00; // Slave bit
    return 0;
}

static int ata_disk_id_to_physical(unsigned char disk_id, unsigned char* ata_id) {
    unsigned char physical_id = disk_id;

    if (!ata_id) {
        return -1;
    }

    if (boot_media.is_floppy) {
        if (disk_id == 0) {
            return -1;
        }
        physical_id = (unsigned char)(disk_id - 1);
    }

    if (physical_id > 3) {
        return -1;
    }

    *ata_id = physical_id;
    return 0;
}

static int ata_decode_target(unsigned char disk_id, ata_target_t* target) {
    unsigned char ata_id;
    if (ata_disk_id_to_physical(disk_id, &ata_id) != 0) {
        return -1;
    }
    return ata_decode_physical_target(ata_id, target);
}

static int ata_has_error(const ata_target_t* target) {
    return (inb(target->io_base + ATA_STATUS) & ATA_SR_ERR) != 0;
}

static void ata_wait_pump(unsigned int timeout) {
    if ((timeout & ATA_WAIT_SPLASH_POLL_MASK) == 0) {
        boot_splash_pump();
    }
}

static int ata_wait_bsy(const ata_target_t* target) {
    unsigned int timeout = 1000000;
    while ((inb(target->io_base + ATA_STATUS) & ATA_SR_BSY) && timeout > 0) {
        ata_wait_pump(timeout);
        timeout--;
    }
    return timeout > 0 ? 0 : -1;
}

static int ata_wait_drdy(const ata_target_t* target) {
    unsigned int timeout = 1000000;
    while (!(inb(target->io_base + ATA_STATUS) & ATA_SR_DRDY) && timeout > 0) {
        ata_wait_pump(timeout);
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
        ata_wait_pump(timeout);
        timeout--;
    }
    return -1;
}

static int ata_probe_physical_target(unsigned char ata_id) {
    ata_target_t target;
    unsigned char status;

    if (ata_decode_physical_target(ata_id, &target) != 0) {
        return 0;
    }

    outb(target.io_base + ATA_DRIVE, (unsigned char)(0xE0 | target.drive_sel));
    io_wait();
    io_wait();
    io_wait();
    io_wait();

    status = inb(target.io_base + ATA_STATUS);
    if (status == 0x00 || status == 0xFF) {
        return 0;
    }

    if (ata_wait_bsy(&target) != 0) {
        return 0;
    }

    outb(target.io_base + ATA_SECCOUNT, 0);
    outb(target.io_base + ATA_LBALO, 0);
    outb(target.io_base + ATA_LBAMID, 0);
    outb(target.io_base + ATA_LBAHI, 0);
    outb(target.io_base + ATA_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();
    io_wait();
    io_wait();
    io_wait();

    status = inb(target.io_base + ATA_STATUS);
    if (status == 0x00 || status == 0xFF) {
        return 0;
    }

    if (ata_wait_bsy(&target) != 0) {
        return 0;
    }

    if (inb(target.io_base + ATA_LBAMID) != 0 || inb(target.io_base + ATA_LBAHI) != 0) {
        return 0;
    }

    if (ata_wait_drq(&target) != 0 || ata_has_error(&target)) {
        return 0;
    }

    insw(target.io_base + ATA_DATA, ata_identify_buffer, 256);
    return 1;
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

static int boot_floppy_lba_to_chs(
    unsigned int lba,
    unsigned short* cylinder,
    unsigned short* head,
    unsigned short* sector
) {
    unsigned int sectors_per_track = boot_media.sectors_per_track;
    unsigned int heads = boot_media.heads;
    unsigned int tmp;

    if (!cylinder || !head || !sector) {
        return -1;
    }
    if (!boot_media.is_floppy || sectors_per_track == 0 || heads == 0) {
        return -1;
    }
    if (boot_media.total_sectors != 0 && lba >= boot_media.total_sectors) {
        return -1;
    }

    tmp = lba / sectors_per_track;
    *sector = (unsigned short)((lba % sectors_per_track) + 1);
    *head = (unsigned short)(tmp % heads);
    *cylinder = (unsigned short)(tmp / heads);
    return 0;
}

static int boot_floppy_rw_sector(unsigned int lba, unsigned char* buffer, int write) {
    unsigned short cylinder = 0;
    unsigned short head = 0;
    unsigned short sector = 0;
    int rc;

    if (!buffer) {
        return -1;
    }
    if (boot_floppy_lba_to_chs(lba, &cylinder, &head, &sector) != 0) {
        return -1;
    }

    if (write) {
        log_serial_raw("DISK111 floppy-write-start\n");
        for (int i = 0; i < BIOS_SECTOR_SIZE; i++) {
            biosdisk_transfer_buffer[i] = buffer[i];
        }
        rc = biosdisk_boot_write_sector(boot_media.drive_number, cylinder, head, sector);
        log_serial_raw(rc == 0 ? "DISK112 floppy-write-ok\n" : "DISK113 floppy-write-fail\n");
        return rc == 0 ? 0 : -1;
    }

    log_serial_raw("DISK101 floppy-read-start\n");
    rc = biosdisk_boot_read_sector(boot_media.drive_number, cylinder, head, sector);
    if (rc != 0) {
        log_serial_raw("DISK102 floppy-read-fail\n");
        return -1;
    }
    log_serial_raw("DISK103 floppy-read-ok\n");
    for (int i = 0; i < BIOS_SECTOR_SIZE; i++) {
        buffer[i] = biosdisk_transfer_buffer[i];
    }
    return 0;
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
    unsigned char boot_flags;

    log_serial_raw("DISK100 disk-init-start\n");
    boot_media.drive_number = read_phys_u8(BOOT_DRIVE_NUMBER_ADDR);
    boot_media.sectors_per_track = read_phys_u16(BOOT_DRIVE_SPT_ADDR);
    boot_media.heads = read_phys_u16(BOOT_DRIVE_HEADS_ADDR);
    boot_media.total_sectors = 0;
    boot_media.is_floppy = boot_media.drive_number < 0x80;
    boot_flags = read_phys_u8(BOOT_DRIVE_FLAGS_ADDR);
    boot_media.geometry_valid = (boot_flags & BOOT_GEOMETRY_VALID_FLAG) != 0;
    ata_presence_mask = 0;

    if (boot_media.is_floppy) {
        unsigned char boot_sector[BIOS_SECTOR_SIZE];

        if (!boot_media.geometry_valid) {
            boot_media.sectors_per_track = 18;
            boot_media.heads = 2;
            boot_media.geometry_valid = 1;
        }

        if (boot_floppy_rw_sector(0, boot_sector, 0) == 0) {
            log_serial_raw("DISK120 floppy-boot-sector-ready\n");
            unsigned int total16 = read_le16(boot_sector + 19);
            unsigned int total32 = read_le32(boot_sector + 32);

            if (boot_media.sectors_per_track == 0) {
                boot_media.sectors_per_track = read_le16(boot_sector + 24);
            }
            if (boot_media.heads == 0) {
                boot_media.heads = read_le16(boot_sector + 26);
            }
            boot_media.total_sectors = total16 != 0 ? total16 : total32;
        }
    }

    for (unsigned char ata_id = 0; ata_id < 4; ata_id++) {
        log_serial_raw("DISK130 ata-probe\n");
        if (ata_probe_physical_target(ata_id)) {
            ata_presence_mask |= (unsigned char)(1u << ata_id);
        }
    }
    log_serial_raw("DISK190 disk-init-ok\n");
}

int disk_boot_media_is_floppy(void) {
    return boot_media.is_floppy;
}

unsigned int disk_boot_media_total_sectors(void) {
    return boot_media.total_sectors;
}

int disk_is_present(unsigned char disk_id) {
    unsigned char ata_id;

    if (boot_media.is_floppy && disk_id == 0) {
        return 1;
    }
    if (ata_disk_id_to_physical(disk_id, &ata_id) != 0) {
        return 0;
    }
    return (ata_presence_mask & (unsigned char)(1u << ata_id)) != 0;
}

int disk_read_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer) {
    ata_target_t target;

    if (boot_media.is_floppy && disk_id == 0) {
        return boot_floppy_rw_sector(lba, buffer, 0);
    }
    if (!disk_is_present(disk_id)) {
        return -1;
    }
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

    if (boot_media.is_floppy && disk_id == 0) {
        return boot_floppy_rw_sector(lba, buffer, 1);
    }
    if (!disk_is_present(disk_id)) {
        return -1;
    }
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

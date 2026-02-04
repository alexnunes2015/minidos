#include "disk.h"

// ATA PIO ports
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_DCR_AS  0x3F6

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

#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01

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

static int ata_wait_bsy() {
    unsigned int timeout = 1000000;
    while ((inb(ATA_PRIMARY_IO + ATA_STATUS) & ATA_SR_BSY) && timeout > 0) {
        timeout--;
    }
    return timeout > 0 ? 0 : -1;
}

static int ata_wait_drq() {
    unsigned int timeout = 1000000;
    while (!(inb(ATA_PRIMARY_IO + ATA_STATUS) & ATA_SR_DRQ) && timeout > 0) {
        timeout--;
    }
    return timeout > 0 ? 0 : -1;
}

void disk_init() {
    // Simple initialization - select master drive
    outb(ATA_PRIMARY_IO + ATA_DRIVE, 0xE0);
}

int disk_read_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer) {
    // For now, only support primary master (disk 0)
    // disk_id: 0=primary master, 1=primary slave, 2=secondary master, 3=secondary slave
    if (disk_id != 0) {
        return -1;  // Only primary master supported for now
    }
    return disk_read_lba(lba, buffer);
}

int disk_read_lba(unsigned int lba, unsigned char* buffer) {
    // Wait for drive to be ready
    if (ata_wait_bsy() != 0) {
        return -1;
    }
    
    // Select drive (master, LBA mode)
    outb(ATA_PRIMARY_IO + ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Send sector count (1 sector)
    outb(ATA_PRIMARY_IO + ATA_SECCOUNT, 1);
    
    // Send LBA
    outb(ATA_PRIMARY_IO + ATA_LBALO, (unsigned char)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_LBAMID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_LBAHI, (unsigned char)((lba >> 16) & 0xFF));
    
    // Send READ command
    outb(ATA_PRIMARY_IO + ATA_COMMAND, ATA_CMD_READ_PIO);
    
    // Wait for data ready
    if (ata_wait_drq() != 0) {
        return -1;
    }
    
    // Check for errors
    if (inb(ATA_PRIMARY_IO + ATA_STATUS) & ATA_SR_ERR) {
        return -1;
    }
    
    // Read 256 words (512 bytes)
    insl(ATA_PRIMARY_IO + ATA_DATA, (unsigned int*)buffer, 128);
    
    return 0;
}

int disk_write_lba(unsigned int lba, unsigned char* buffer) {
    // TODO: Implement write
    (void)lba;
    (void)buffer;
    return -1;
}

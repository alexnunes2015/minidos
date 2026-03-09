#ifndef DRIVE_H
#define DRIVE_H

#define MAX_DRIVES 26  // A: to Z:
#define SECTOR_SIZE 512

typedef struct {
    unsigned char active;           // 0x80 = bootable
    unsigned char start_head;
    unsigned char start_sector;     // bits 0-5 are sector
    unsigned char start_cylinder;
    unsigned char partition_type;   // 0x06=FAT16, 0x0B=FAT32, etc
    unsigned char end_head;
    unsigned char end_sector;
    unsigned char end_cylinder;
    unsigned int lba_start;         // LBA of first sector
    unsigned int sector_count;      // Number of sectors
} __attribute__((packed)) MBR_PartitionEntry;

typedef struct {
    unsigned char boot_code[446];
    MBR_PartitionEntry partitions[4];
    unsigned short signature;       // 0xAA55
} __attribute__((packed)) MBR;

typedef struct {
    int valid;                      // Is this drive slot valid?
    unsigned char disk_id;          // 0=primary master, 1=primary slave, etc
    int partition_num;              // Partition number (0-3) or -1 for whole disk
    unsigned int lba_start;         // Starting LBA sector
    unsigned int sector_count;      // Total sectors
    unsigned char fs_type;          // Filesystem type (0x06=FAT16, etc)
} DriveInfo;

// Initialize drive system and detect all drives
void drive_init();

// Get drive info for a drive letter (0=A:, 1=B:, etc)
DriveInfo* drive_get_info(int drive_letter);

// Read sector from a specific drive
int drive_read_sector(int drive_letter, unsigned int lba, unsigned char* buffer);

// Write sector to a specific drive
int drive_write_sector(int drive_letter, unsigned int lba, unsigned char* buffer);

// Get current drive letter
int drive_get_current();

// Set current drive
void drive_set_current(int drive_letter);

// Print list of all detected drives
void drive_list_all();
int drive_get_count();

#endif

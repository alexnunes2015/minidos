#include "drive.h"
#include "boot_splash.h"
#include "disk.h"
#include "video.h"
#include "serial.h"

static DriveInfo drives[MAX_DRIVES];
static int current_drive = 0;  // A:
static unsigned char mbr_buffer[SECTOR_SIZE];

static void drive_boot_print(const char* str) {
    if (!boot_splash_is_active()) {
        print_string(str);
    }
}

static unsigned short read_le16(const unsigned char* ptr) {
    return (unsigned short)(ptr[0] | (ptr[1] << 8));
}

static unsigned int read_le32(const unsigned char* ptr) {
    return (unsigned int)(ptr[0] |
                          (ptr[1] << 8) |
                          (ptr[2] << 16) |
                          (ptr[3] << 24));
}

// Helper to print hex byte
static void print_hex_byte(unsigned char val) {
    const char hex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[val >> 4];
    buf[1] = hex[val & 0x0F];
    buf[2] = '\0';
    print_string(buf);
}

// Helper to print number
static void print_num(unsigned int num) {
    if (num == 0) {
        print_char('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) {
        print_char(buf[--i]);
    }
}

static void drive_boot_print_num(unsigned int num) {
    if (!boot_splash_is_active()) {
        print_num(num);
    }
}

static void drive_reset_slots(void) {
    for (int i = 0; i < MAX_DRIVES; i++) {
        drives[i].valid = 0;
    }
    current_drive = 0;
}

static int drive_next_letter_index(void) {
    int next_letter = 0;
    while (next_letter < MAX_DRIVES && drives[next_letter].valid) {
        next_letter++;
    }
    return next_letter;
}

static void drive_select_first_valid(void) {
    current_drive = 0;
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (drives[i].valid) {
            current_drive = i;
            return;
        }
    }
}

static unsigned char detect_fat_type(const unsigned char* boot_sector) {
    unsigned short bytes_per_sector = read_le16(boot_sector + 11);
    unsigned char sectors_per_cluster = boot_sector[13];
    unsigned short reserved_sectors = read_le16(boot_sector + 14);
    unsigned char fat_count = boot_sector[16];
    unsigned short root_entries = read_le16(boot_sector + 17);
    unsigned short total_sectors_16 = read_le16(boot_sector + 19);
    unsigned short sectors_per_fat = read_le16(boot_sector + 22);
    unsigned int total_sectors_32 = read_le32(boot_sector + 32);
    unsigned int total_sectors = total_sectors_16 != 0 ? total_sectors_16 : total_sectors_32;
    unsigned int root_dir_sectors;
    unsigned int data_sectors;
    unsigned int cluster_count;

    if (bytes_per_sector != SECTOR_SIZE ||
        sectors_per_cluster == 0 ||
        reserved_sectors == 0 ||
        fat_count == 0 ||
        sectors_per_fat == 0 ||
        root_entries == 0 ||
        total_sectors == 0) {
        return 0;
    }

    root_dir_sectors = ((unsigned int)root_entries * 32U + (bytes_per_sector - 1)) / bytes_per_sector;
    if (total_sectors <= (unsigned int)reserved_sectors + (unsigned int)fat_count * sectors_per_fat + root_dir_sectors) {
        return 0;
    }

    data_sectors = total_sectors - ((unsigned int)reserved_sectors + (unsigned int)fat_count * sectors_per_fat + root_dir_sectors);
    cluster_count = data_sectors / sectors_per_cluster;

    if (cluster_count < 4085U) {
        return 0x01; // FAT12
    }
    if (cluster_count < 65525U) {
        return 0x06; // FAT16
    }
    return 0;
}

static int detect_whole_disk_volume(unsigned char disk_id, int* next_letter) {
    unsigned short total_sectors_16;
    unsigned int total_sectors_32;
    unsigned int total_sectors;
    unsigned char fs_type;

    if (!next_letter || *next_letter >= MAX_DRIVES) {
        return 0;
    }
    if (disk_read_lba_from_disk(disk_id, 0, mbr_buffer) != 0) {
        return 0;
    }
    if (mbr_buffer[510] != 0x55 || mbr_buffer[511] != 0xAA) {
        return 0;
    }

    fs_type = detect_fat_type(mbr_buffer);
    if (fs_type == 0) {
        return 0;
    }

    total_sectors_16 = read_le16(mbr_buffer + 19);
    total_sectors_32 = read_le32(mbr_buffer + 32);
    total_sectors = total_sectors_16 != 0 ? total_sectors_16 : total_sectors_32;
    if (total_sectors == 0 && disk_id == 0) {
        total_sectors = disk_boot_media_total_sectors();
    }
    if (total_sectors == 0) {
        return 0;
    }

    drives[*next_letter].valid = 1;
    drives[*next_letter].disk_id = disk_id;
    drives[*next_letter].partition_num = -1;
    drives[*next_letter].lba_start = 0;
    drives[*next_letter].sector_count = total_sectors;
    drives[*next_letter].fs_type = fs_type;
    (*next_letter)++;
    return 1;
}

// Detect partitions on a specific disk
static int detect_disk_partitions(unsigned char disk_id, int* next_letter) {
    // Try to read MBR from this disk
    if (disk_read_lba_from_disk(disk_id, 0, mbr_buffer) != 0) {
        return 0;  // Disk not present or error
    }
    
    MBR* mbr = (MBR*)mbr_buffer;
    
    // Check MBR signature - if valid, read partition table
    if (mbr->signature != 0xAA55) {
        // No valid MBR signature, can't read partitions
        return 0;
    }
    
    int partitions_found = 0;
    
    // Scan partition table
    for (int i = 0; i < 4; i++) {
        MBR_PartitionEntry* part = &mbr->partitions[i];
        
        // Skip empty partitions
        if (part->partition_type == 0 || part->sector_count == 0) {
            continue;
        }
        
        // Skip extended partitions (for now)
        if (part->partition_type == 0x05 || part->partition_type == 0x0F) {
            continue;
        }
        
        // Assign drive letter
        if (*next_letter < MAX_DRIVES) {
            drives[*next_letter].valid = 1;
            drives[*next_letter].disk_id = disk_id;
            drives[*next_letter].partition_num = i;
            drives[*next_letter].lba_start = part->lba_start;
            drives[*next_letter].sector_count = part->sector_count;
            drives[*next_letter].fs_type = part->partition_type;
            
            (*next_letter)++;
            partitions_found++;
        }
    }
    
    return partitions_found;
}

void drive_init() {
    drive_init_boot_media();
    drive_probe_additional();
}

void drive_init_boot_media() {
    int next_letter = 0;

    drive_reset_slots();

    if (disk_boot_media_is_floppy()) {
        if (detect_whole_disk_volume(0, &next_letter)) {
            drive_boot_print("  Boot floppy: whole-disk volume\n");
        }
    }
    drive_select_first_valid();
}

void drive_probe_additional() {
    int next_letter = drive_next_letter_index();

    drive_boot_print("Detecting drives...\n");

    // Try to detect up to 4 ATA disks (primary master/slave, secondary master/slave).
    for (unsigned char disk = disk_boot_media_is_floppy() ? 1 : 0; disk < 4; disk++) {
        if (!disk_is_present(disk)) {
            continue;
        }

        int found = detect_disk_partitions(disk, &next_letter);
        if (found > 0) {
            drive_boot_print("  Disk ");
            drive_boot_print_num(disk);
            drive_boot_print(": ");
            drive_boot_print_num(found);
            drive_boot_print(" partition(s)\n");
        }
    }
    
    if (next_letter == 0) {
        if (disk_boot_media_is_floppy() && detect_whole_disk_volume(0, &next_letter)) {
            drive_boot_print("  Boot floppy fallback mounted as A:\n");
        } else {
            serial_print("DISK021 no-boot-volume\n");
            drive_boot_print("  No boot volume or partitions found\n");
        }
    }

    drive_select_first_valid();

    drive_boot_print("Total drives: ");
    drive_boot_print_num((unsigned int)next_letter);
    drive_boot_print("\n\n");
}

DriveInfo* drive_get_info(int drive_letter) {
    if (drive_letter < 0 || drive_letter >= MAX_DRIVES) {
        return 0;
    }
    if (!drives[drive_letter].valid) {
        return 0;
    }
    return &drives[drive_letter];
}

int drive_read_sector(int drive_letter, unsigned int lba, unsigned char* buffer) {
    DriveInfo* info = drive_get_info(drive_letter);
    if (!info) {
        return -1;
    }
    
    // Calculate absolute LBA
    unsigned int absolute_lba = info->lba_start + lba;
    
    // Read from the specific disk
    return disk_read_lba_from_disk(info->disk_id, absolute_lba, buffer);
}

int drive_write_sector(int drive_letter, unsigned int lba, unsigned char* buffer) {
    DriveInfo* info = drive_get_info(drive_letter);
    if (!info) {
        return -1;
    }

    unsigned int absolute_lba = info->lba_start + lba;

    return disk_write_lba_from_disk(info->disk_id, absolute_lba, buffer);
}

int drive_get_current() {
    if (current_drive < 0 || current_drive >= MAX_DRIVES || !drives[current_drive].valid) {
        for (int i = 0; i < MAX_DRIVES; i++) {
            if (drives[i].valid) {
                current_drive = i;
                return current_drive;
            }
        }
        current_drive = 0;
    }
    return current_drive;
}

void drive_set_current(int drive_letter) {
    if (drive_letter >= 0 && drive_letter < MAX_DRIVES && drives[drive_letter].valid) {
        current_drive = drive_letter;
    }
}

void drive_list_all() {
    serial_print("Available drives:\n");
    print_string("Available drives:\n\n");
    
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (!drives[i].valid) {
            continue;
        }
        
        // Print drive letter
        print_char('A' + i);
        print_string(": ");
        
        // Print filesystem type
        print_string("Type 0x");
        print_hex_byte(drives[i].fs_type);
        print_string(" ");
        
        // Print size in MB
        unsigned int size_mb = (drives[i].sector_count / 2048);
        if (size_mb > 0) {
            print_string("(");
            print_num(size_mb);
            print_string(" MB)");
        }
        
        // Print disk info
        print_string(" [Disk ");
        print_num(drives[i].disk_id);
        if (drives[i].partition_num >= 0) {
            print_string(", Partition ");
            print_num(drives[i].partition_num + 1);
        }
        print_string("]");
        
        print_char('\n');
    }
}

int drive_get_count() {
    int count = 0;
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (drives[i].valid) {
            count++;
        }
    }
    return count;
}

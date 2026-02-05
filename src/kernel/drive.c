#include "drive.h"
#include "disk.h"
#include "video.h"
#include "serial.h"

static DriveInfo drives[MAX_DRIVES];
static int current_drive = 0;  // A:
static unsigned char mbr_buffer[SECTOR_SIZE];

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
        if (part->partition_type == 0) {
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
    // Clear all drive slots
    for (int i = 0; i < MAX_DRIVES; i++) {
        drives[i].valid = 0;
    }
    
    int next_letter = 0;
    
    print_string("Detecting drives...\n");
    
    // Try to detect up to 4 ATA disks (primary master/slave, secondary master/slave)
    for (unsigned char disk = 0; disk < 4; disk++) {
        int found = detect_disk_partitions(disk, &next_letter);
        if (found > 0) {
            print_string("  Disk ");
            print_num(disk);
            print_string(": ");
            print_num(found);
            print_string(" partition(s)\n");
        }
    }
    
    if (next_letter == 0) {
        print_string("  No partitions found - creating test drive A:\n");
        drives[0].valid = 1;
        drives[0].disk_id = 0;
        drives[0].partition_num = -1;
        drives[0].lba_start = 2048;  // 1MB offset
        drives[0].sector_count = 32768;  // 16MB
        drives[0].fs_type = 0x06;  // FAT16
        next_letter = 1;
    }
    
    // Set current drive to first valid drive (default A:)
    current_drive = 0;
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (drives[i].valid) {
            current_drive = i;
            break;
        }
    }

    print_string("Total drives: ");
    print_num(next_letter);
    print_string("\n\n");
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

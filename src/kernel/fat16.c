#include "fat16.h"
#include "video.h"
#include "drive.h"

static FAT16_BPB bpb;
static unsigned char sector_buffer[SECTOR_SIZE];

static int current_drive_letter = 0;  // A:

int disk_read_sector(int lba, unsigned char* buffer) {
    // Use drive manager to read from current drive
    return drive_read_sector(current_drive_letter, (unsigned int)lba, buffer);
}

void fat16_set_drive(int drive_letter) {
    current_drive_letter = drive_letter;
}

void fat16_init() {
    // Read boot sector to get BPB
    if (disk_read_sector(0, sector_buffer) != 0) {
        print_string("[FAT16] Warning: No disk or FAT16 partition found\n");
        return;
    }
    
    // Copy BPB (starts at offset 11 in boot sector)
    unsigned char* bpb_ptr = sector_buffer + 11;
    bpb.bytes_per_sector = *(unsigned short*)(bpb_ptr + 0);
    bpb.sectors_per_cluster = *(bpb_ptr + 2);
    bpb.reserved_sectors = *(unsigned short*)(bpb_ptr + 3);
    bpb.fat_count = *(bpb_ptr + 5);
    bpb.root_entries = *(unsigned short*)(bpb_ptr + 6);
    bpb.total_sectors_16 = *(unsigned short*)(bpb_ptr + 8);
    bpb.media_type = *(bpb_ptr + 10);
    bpb.sectors_per_fat = *(unsigned short*)(bpb_ptr + 11);
}

static int get_root_dir_start() {
    return bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat);
}

static int get_data_start() {
    int root_dir_sectors = ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
    return get_root_dir_start() + root_dir_sectors;
}

void fat16_list_root() {
    int root_start = get_root_dir_start();
    int root_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
    
    // Print directory header with current drive letter
    print_string("Directory of ");
    print_char('A' + current_drive_letter);
    print_string(":\\\n\n");
    
    for (int sector = 0; sector < root_sectors; sector++) {
        disk_read_sector(root_start + sector, sector_buffer);
        FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
        
        for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
            if (entries[i].name[0] == 0x00) return; // End of directory
            if (entries[i].name[0] == 0xE5) continue; // Deleted
            if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
            
            // Print filename
            for (int j = 0; j < 8; j++) {
                if (entries[i].name[j] != ' ')
                    print_char(entries[i].name[j]);
            }
            
            if (entries[i].name[8] != ' ') {
                print_char('.');
                for (int j = 8; j < 11; j++) {
                    if (entries[i].name[j] != ' ')
                        print_char(entries[i].name[j]);
                }
            }
            
            // Print attributes
            print_string("  ");
            if (entries[i].attributes & FAT16_ATTR_DIRECTORY) {
                print_string("<DIR>");
            } else {
                print_string("     ");
            }
            print_char('\n');
        }
    }
}

int fat16_read_file(const char* filename, unsigned char* buffer, int max_size) {
    // Find the file in root directory
    int root_start = get_root_dir_start();
    int root_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
    
    for (int sector = 0; sector < root_sectors; sector++) {
        disk_read_sector(root_start + sector, sector_buffer);
        FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
        
        for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
            if (entries[i].name[0] == 0x00) return 0; // Not found
            if (entries[i].name[0] == 0xE5) continue; // Deleted
            if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue; // Skip dirs
            
            // Compare filename (8.3 format)
            int match = 1;
            int name_len = 0;
            
            // Match name part
            for (int j = 0; j < 8; j++) {
                if (filename[name_len] == '.' || filename[name_len] == '\0') break;
                if (entries[i].name[j] != filename[name_len]) {
                    match = 0;
                    break;
                }
                name_len++;
            }
            
            if (!match) continue;
            
            // Skip spaces in directory entry name
            int dir_name_end = 8;
            while (dir_name_end > 0 && entries[i].name[dir_name_end-1] == ' ') dir_name_end--;
            
            // Check for dot and extension
            if (filename[name_len] == '.') {
                name_len++;
                int ext_pos = 8;
                while (filename[name_len] && ext_pos < 11) {
                    if (entries[i].name[ext_pos] != filename[name_len]) {
                        match = 0;
                        break;
                    }
                    ext_pos++;
                    name_len++;
                }
            } else if (dir_name_end < 8) {
                // Filename without extension matches if directory entry extension is spaces
                int ext_pos = 8;
                while (ext_pos < 11 && entries[i].name[ext_pos] == ' ') ext_pos++;
                if (ext_pos < 11) match = 0;
            }
            
            if (!match) continue;
            if (filename[name_len] != '\0') continue;
            
            // File found! Read it
            unsigned int file_size = entries[i].file_size;
            unsigned int cluster = entries[i].cluster_low;
            unsigned int bytes_read = 0;
            
            // Read the file by following the FAT chain
            while (cluster != 0xFFFF && bytes_read < (unsigned)max_size && file_size > 0) {
                // Calculate sector for this cluster
                int data_start = get_root_dir_start();
                int root_dir_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
                int cluster_sector = data_start + root_dir_sectors + (cluster - 2) * bpb.sectors_per_cluster;
                
                // Read cluster
                int bytes_to_read = file_size > 512 ? 512 : file_size;
                disk_read_sector(cluster_sector, sector_buffer);
                
                // Copy to buffer
                for (int j = 0; j < bytes_to_read && bytes_read < (unsigned)max_size; j++) {
                    buffer[bytes_read++] = sector_buffer[j];
                }
                
                file_size -= bytes_to_read;
                
                // Follow FAT chain
                unsigned int fat_sector = bpb.reserved_sectors + (cluster * 2) / 512;
                unsigned int fat_offset = (cluster * 2) % 512;
                disk_read_sector(fat_sector, sector_buffer);
                cluster = *(unsigned short*)(sector_buffer + fat_offset);
            }
            
            return bytes_read;
        }
    }
    
    return 0;  // File not found
}

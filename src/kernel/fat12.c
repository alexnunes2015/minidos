#include "fat12.h"
#include "video.h"

// Constants for 1.44MB Floppy
#define SECTOR_SIZE 512
#define ROOT_DIR_START 19
#define ROOT_DIR_SECTORS 14

// Placeholder for disk reading
// In a real kernel, this would talk to the Floppy Controller or use a BIOS bridge
int disk_read_sector(int lba, unsigned char* buffer) {
    // This is where BIOS INT 13h or FDC I/O would go.
    // For the MVP, we assume the bootloader might have pre-loaded some data
    // or this is linked against a BIOS stub.
    return 0; 
}

void fat_list_root() {
    unsigned char buffer[SECTOR_SIZE];
    FAT_DirectoryEntry* entry;

    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        disk_read_sector(ROOT_DIR_START + s, buffer);
        for (int i = 0; i < SECTOR_SIZE / sizeof(FAT_DirectoryEntry); i++) {
            entry = (FAT_DirectoryEntry*)&buffer[i * sizeof(FAT_DirectoryEntry)];
            
            if (entry->name[0] == 0x00) return; // End of list
            if (entry->name[0] == 0xE5) continue; // Deleted
            if (entry->attr & 0x08) continue; // Volume Label

            // Print Name.Ext
            for (int j = 0; j < 8; j++) {
                if (entry->name[j] != ' ') print_char(entry->name[j]);
            }
            print_char('.');
            for (int j = 0; j < 3; j++) {
                if (entry->ext[j] != ' ') print_char(entry->ext[j]);
            }
            print_string("    ");
            
            // Size (simplified)
            // print_int(entry->size); // We'd need a print_int helper
            print_string("\n");
        }
    }
}

void fat_type_file(const char* filename) {
    // Search in root directory (simplified)
    print_string("File content display not fully implemented in MVP shell stub.\n");
}

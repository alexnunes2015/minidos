#include "fat16.h"
#include "video.h"
#include "drive.h"
#include "serial.h"

static FAT16_BPB bpb;
static unsigned char sector_buffer[SECTOR_SIZE];

static int current_drive_letter = 0;  // A:
static int fat16_debug_enabled = 1;

static int fat16_alloc_cluster_internal(unsigned short* out_cluster);
static int fat16_free_cluster_chain_internal(unsigned short start_cluster);
static int fat16_find_entry_internal(
    unsigned int dir_cluster,
    const char* name,
    FAT16_DirectoryEntry* out_entry,
    int* out_sector,
    int* out_index
);

static void print_both_char(char c) {
    print_char(c);
    serial_putchar(c);
}

static void print_both_string(const char* s) {
    print_string(s);
    serial_print(s);
}

static void print_entry_name(const FAT16_DirectoryEntry* entry) {
    int ext_present = 0;
    for (int j = 8; j < 11; j++) {
        if (entry->name[j] != ' ') {
            ext_present = 1;
            break;
        }
    }

    for (int j = 0; j < 8; j++) {
        print_both_char(entry->name[j]);
    }

    if (ext_present) {
        print_both_char('.');
        for (int j = 8; j < 11; j++) {
            print_both_char(entry->name[j]);
        }
    } else {
        print_both_string("    ");
    }
}

int disk_read_sector(int lba, unsigned char* buffer) {
    // Use drive manager to read from current drive
    return drive_read_sector(current_drive_letter, (unsigned int)lba, buffer);
}
static int disk_write_sector(int lba, unsigned char* buffer) {
    return drive_write_sector(current_drive_letter, (unsigned int)lba, buffer);
}

void fat16_set_drive(int drive_letter) {
    current_drive_letter = drive_letter;
}

int fat16_init() {
    if (fat16_debug_enabled) {
        serial_print("[FAT16] init drive=");
        serial_putchar((char)('A' + current_drive_letter));
        serial_print(" lba=0\n");
    }

    // Read boot sector to get BPB
    if (disk_read_sector(0, sector_buffer) != 0) {
        print_string("[FAT16] Warning: No disk or FAT16 partition found\n");
        if (fat16_debug_enabled) {
            serial_print("[FAT16] read sector 0 failed\n");
        }
        return 0;
    }

    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        print_string("[FAT16] Warning: Invalid boot sector signature\n");
        if (fat16_debug_enabled) {
            serial_print("[FAT16] bad sig: ");
            serial_print_hex((unsigned int)((sector_buffer[511] << 8) | sector_buffer[510]));
            serial_print("\n");
        }
        return 0;
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
    bpb.sectors_per_track = *(unsigned short*)(bpb_ptr + 13);
    bpb.heads = *(unsigned short*)(bpb_ptr + 15);
    bpb.hidden_sectors = *(unsigned int*)(bpb_ptr + 17);
    bpb.total_sectors_32 = *(unsigned int*)(bpb_ptr + 21);

    if (bpb.bytes_per_sector == 0 || bpb.sectors_per_fat == 0 || bpb.root_entries == 0) {
        print_string("[FAT16] Warning: Invalid BPB values\n");
        if (fat16_debug_enabled) {
            serial_print("[FAT16] invalid bpb: bps=");
            serial_print_hex(bpb.bytes_per_sector);
            serial_print(" spf=");
            serial_print_hex(bpb.sectors_per_fat);
            serial_print(" root=");
            serial_print_hex(bpb.root_entries);
            serial_print("\n");
        }
        return 0;
    }

    if (fat16_debug_enabled) {
        serial_print("[FAT16] ok bps=");
        serial_print_hex(bpb.bytes_per_sector);
        serial_print(" spf=");
        serial_print_hex(bpb.sectors_per_fat);
        serial_print(" root=");
        serial_print_hex(bpb.root_entries);
        serial_print("\n");
    }

    return 1;
}

static int get_root_dir_start() {
    return bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat);
}

static int get_data_start() {
    int root_dir_sectors = ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
    return get_root_dir_start() + root_dir_sectors;
}
static int get_root_dir_sectors() {
    return ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
}

static unsigned int get_total_sectors() {
    if (bpb.total_sectors_16 != 0) {
        return bpb.total_sectors_16;
    }
    return bpb.total_sectors_32;
}

static unsigned int get_total_clusters() {
    unsigned int data_sectors = get_total_sectors() - (bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat) + get_root_dir_sectors());
    return data_sectors / bpb.sectors_per_cluster;
}

static unsigned short fat16_read_fat_entry(unsigned short cluster) {
    unsigned int fat_sector = bpb.reserved_sectors + (cluster * 2) / SECTOR_SIZE;
    unsigned int fat_offset = (cluster * 2) % SECTOR_SIZE;
    if (disk_read_sector((int)fat_sector, sector_buffer) != 0) {
        return 0xFFFF;
    }
    return *(unsigned short*)(sector_buffer + fat_offset);
}

static int fat16_write_fat_entry(unsigned short cluster, unsigned short value) {
    unsigned int fat_offset = (cluster * 2) % SECTOR_SIZE;
    unsigned int fat_sector_index = (cluster * 2) / SECTOR_SIZE;

    for (unsigned int fat = 0; fat < bpb.fat_count; fat++) {
        unsigned int fat_sector = bpb.reserved_sectors + fat * bpb.sectors_per_fat + fat_sector_index;
        if (disk_read_sector((int)fat_sector, sector_buffer) != 0) {
            return 0;
        }
        *(unsigned short*)(sector_buffer + fat_offset) = value;
        if (disk_write_sector((int)fat_sector, sector_buffer) != 0) {
            return 0;
        }
    }

    return 1;
}

static unsigned short fat16_get_next_cluster(unsigned short cluster) {
    unsigned int fat_sector = bpb.reserved_sectors + (cluster * 2) / SECTOR_SIZE;
    unsigned int fat_offset = (cluster * 2) % SECTOR_SIZE;
    disk_read_sector((int)fat_sector, sector_buffer);
    return *(unsigned short*)(sector_buffer + fat_offset);
}

static int fat16_match_name(const char* filename, const FAT16_DirectoryEntry* entry) {
    if (filename[0] == '.' && filename[1] == '\0') {
        if (entry->name[0] != '.' || entry->name[1] != ' ') return 0;
        for (int i = 2; i < 11; i++) {
            if (entry->name[i] != ' ') return 0;
        }
        return 1;
    }
    if (filename[0] == '.' && filename[1] == '.' && filename[2] == '\0') {
        if (entry->name[0] != '.' || entry->name[1] != '.') return 0;
        for (int i = 2; i < 11; i++) {
            if (entry->name[i] != ' ') return 0;
        }
        return 1;
    }

    int match = 1;
    int name_len = 0;

    for (int j = 0; j < 8; j++) {
        if (filename[name_len] == '.' || filename[name_len] == '\0') break;
        if (entry->name[j] != filename[name_len]) {
            match = 0;
            break;
        }
        name_len++;
    }

    if (!match) return 0;

    int dir_name_end = 8;
    while (dir_name_end > 0 && entry->name[dir_name_end - 1] == ' ') dir_name_end--;

    if (filename[name_len] == '.') {
        name_len++;
        int ext_pos = 8;
        while (filename[name_len] && ext_pos < 11) {
            if (entry->name[ext_pos] != filename[name_len]) {
                match = 0;
                break;
            }
            ext_pos++;
            name_len++;
        }
    } else if (dir_name_end < 8) {
        int ext_pos = 8;
        while (ext_pos < 11 && entry->name[ext_pos] == ' ') ext_pos++;
        if (ext_pos < 11) match = 0;
    }

    if (!match) return 0;
    if (filename[name_len] != '\0') return 0;

    return 1;
}

static int fat16_entry_is_visible(const FAT16_DirectoryEntry* entry) {
    if (entry->name[0] == 0x00) return 0;
    if (entry->name[0] == 0xE5) return 0;
    if (entry->attributes & FAT16_ATTR_VOLUME_ID) return 0;
    if ((entry->attributes & 0x0F) == 0x0F) return 0;
    return 1;
}

static void fat16_build_display_name(const FAT16_DirectoryEntry* entry, char out[13]) {
    int pos = 0;
    int name_end = 8;
    int ext_end = 11;

    while (name_end > 0 && entry->name[name_end - 1] == ' ') name_end--;
    while (ext_end > 8 && entry->name[ext_end - 1] == ' ') ext_end--;

    for (int i = 0; i < name_end; i++) {
        out[pos++] = (char)entry->name[i];
    }
    if (ext_end > 8) {
        out[pos++] = '.';
        for (int i = 8; i < ext_end; i++) {
            out[pos++] = (char)entry->name[i];
        }
    }
    out[pos] = '\0';
}

static char fat16_upper_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int fat16_wildcard_match(const char* pattern, const char* text) {
    if (*pattern == '\0') {
        return *text == '\0';
    }
    if (*pattern == '*') {
        while (*(pattern + 1) == '*') {
            pattern++;
        }
        if (*(pattern + 1) == '\0') {
            return 1;
        }
        const char* t = text;
        while (*t) {
            if (fat16_wildcard_match(pattern + 1, t)) {
                return 1;
            }
            t++;
        }
        return fat16_wildcard_match(pattern + 1, t);
    }
    if (*text == '\0') {
        return 0;
    }
    if (*pattern == '?') {
        return fat16_wildcard_match(pattern + 1, text + 1);
    }
    if (fat16_upper_char(*pattern) != fat16_upper_char(*text)) {
        return 0;
    }
    return fat16_wildcard_match(pattern + 1, text + 1);
}

static int fat16_match_pattern(const char* pattern, const FAT16_DirectoryEntry* entry) {
    char display_name[13];

    if (!pattern || pattern[0] == '\0') {
        return 1;
    }
    if ((pattern[0] == '*' && pattern[1] == '\0') ||
        (pattern[0] == '*' && pattern[1] == '.' && pattern[2] == '*' && pattern[3] == '\0')) {
        return 1;
    }

    fat16_build_display_name(entry, display_name);
    return fat16_wildcard_match(pattern, display_name);
}

void fat16_list_root() {
    fat16_list_root_filtered(0);
}

void fat16_list_root_filtered(const char* pattern) {
    int root_start = get_root_dir_start();
    int root_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;

    for (int sector = 0; sector < root_sectors; sector++) {
        if (disk_read_sector(root_start + sector, sector_buffer) != 0) {
            print_both_string("[FAT16] Error reading root directory\n");
            return;
        }
        FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

        for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
            if (entries[i].name[0] == 0x00) return;
            if (!fat16_entry_is_visible(&entries[i])) continue;
            if (!fat16_match_pattern(pattern, &entries[i])) continue;

            print_entry_name(&entries[i]);

            print_both_string("  ");
            if (entries[i].attributes & FAT16_ATTR_DIRECTORY) {
                print_both_string("<DIR>");
            } else {
                print_both_string("     ");
            }
            print_both_char('\n');
        }
    }
}

void fat16_list_dir(unsigned int dir_cluster) {
    fat16_list_dir_filtered(dir_cluster, 0);
}

void fat16_list_dir_filtered(unsigned int dir_cluster, const char* pattern) {
    if (dir_cluster == 0) {
        fat16_list_root_filtered(pattern);
        return;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                print_both_string("[FAT16] Error reading directory\n");
                return;
            }
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return;
                if (!fat16_entry_is_visible(&entries[i])) continue;
                if (!fat16_match_pattern(pattern, &entries[i])) continue;

                print_entry_name(&entries[i]);

                print_both_string("  ");
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) {
                    print_both_string("<DIR>");
                } else {
                    print_both_string("     ");
                }
                print_both_char('\n');
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }
}

int fat16_read_file(const char* filename, unsigned char* buffer, int max_size) {
    return fat16_read_file_from_dir(0, filename, buffer, max_size);
}

int fat16_write_file(const char* filename, const unsigned char* buffer, int size) {
    return fat16_write_file_from_dir(0, filename, buffer, size);
}

int fat16_write_file_from_dir(unsigned int dir_cluster, const char* filename, const unsigned char* buffer, int size) {
    FAT16_DirectoryEntry existing;
    unsigned short old_first_cluster = 0;
    unsigned short new_first_cluster = 0;
    unsigned short write_cluster = 0;
    unsigned short prev_cluster = 0;
    unsigned int remaining = 0;
    unsigned int max_steps = 0;
    int cluster_bytes = (int)bpb.sectors_per_cluster * SECTOR_SIZE;
    int offset = 0;
    int exists = 0;

    if (!filename || filename[0] == '\0' || size < 0) {
        return 0;
    }
    if (size > 0 && !buffer) {
        return 0;
    }
    if (cluster_bytes <= 0) {
        return 0;
    }

    exists = fat16_find_entry_internal(dir_cluster, filename, &existing, 0, 0);
    if (exists) {
        if (existing.attributes & FAT16_ATTR_DIRECTORY) {
            return 0;
        }
        old_first_cluster = existing.cluster_low;
    }

    remaining = (unsigned int)size;
    max_steps = get_total_clusters() + 2;

    while (remaining > 0 && max_steps-- > 0) {
        int cluster_sector_base;

        if (!fat16_alloc_cluster_internal(&write_cluster)) {
            if (new_first_cluster >= 2) {
                fat16_free_cluster_chain_internal(new_first_cluster);
            }
            return 0;
        }
        if (new_first_cluster == 0) {
            new_first_cluster = write_cluster;
        }
        if (prev_cluster >= 2) {
            if (!fat16_write_fat_entry(prev_cluster, write_cluster)) {
                fat16_free_cluster_chain_internal(new_first_cluster);
                return 0;
            }
            if (!fat16_write_fat_entry(write_cluster, 0xFFFF)) {
                fat16_free_cluster_chain_internal(new_first_cluster);
                return 0;
            }
        }
        prev_cluster = write_cluster;

        cluster_sector_base = get_data_start() + (write_cluster - 2) * bpb.sectors_per_cluster;
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            unsigned int chunk = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
            for (int i = 0; i < SECTOR_SIZE; i++) {
                sector_buffer[i] = 0;
            }
            if (chunk > 0) {
                for (unsigned int i = 0; i < chunk; i++) {
                    sector_buffer[i] = buffer[offset + (int)i];
                }
                offset += (int)chunk;
                remaining -= chunk;
            }
            if (disk_write_sector(cluster_sector_base + s, sector_buffer) != 0) {
                fat16_free_cluster_chain_internal(new_first_cluster);
                return 0;
            }
            if (remaining == 0) {
                // keep writing zeroed sectors for this cluster (already zero-filled)
            }
        }
    }

    if (remaining != 0) {
        if (new_first_cluster >= 2) {
            fat16_free_cluster_chain_internal(new_first_cluster);
        }
        return 0;
    }

    if (exists) {
        if (!fat16_update_entry(dir_cluster, filename, filename, new_first_cluster, (unsigned int)size,
                                (unsigned char)((existing.attributes & FAT16_ATTR_READ_ONLY) | FAT16_ATTR_ARCHIVE))) {
            if (new_first_cluster >= 2) {
                fat16_free_cluster_chain_internal(new_first_cluster);
            }
            return 0;
        }
    } else {
        if (!fat16_create_entry(dir_cluster, filename, new_first_cluster, (unsigned int)size, FAT16_ATTR_ARCHIVE)) {
            if (new_first_cluster >= 2) {
                fat16_free_cluster_chain_internal(new_first_cluster);
            }
            return 0;
        }
    }

    if (old_first_cluster >= 2 && old_first_cluster != new_first_cluster) {
        if (!fat16_free_cluster_chain_internal(old_first_cluster)) {
            return 0;
        }
    }

    return 1;
}

int fat16_read_file_from_dir(unsigned int dir_cluster, const char* filename, unsigned char* buffer, int max_size) {
    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;

        for (int sector = 0; sector < root_sectors; sector++) {
            disk_read_sector(root_start + sector, sector_buffer);
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0; // Not found
                if (entries[i].name[0] == 0xE5) continue; // Deleted
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue; // Skip dirs
                if ((entries[i].attributes & 0x0F) == 0x0F) continue; // LFN

                if (!fat16_match_name(filename, &entries[i])) continue;

                unsigned int file_size = entries[i].file_size;
                unsigned int cluster = entries[i].cluster_low;
                unsigned int bytes_read = 0;

                while (cluster != 0xFFFF && bytes_read < (unsigned)max_size && file_size > 0) {
                    int data_start = get_data_start();
                    int cluster_sector = data_start + (cluster - 2) * bpb.sectors_per_cluster;

                    for (int s = 0; s < bpb.sectors_per_cluster && file_size > 0; s++) {
                        int bytes_to_read = file_size > 512 ? 512 : file_size;
                        disk_read_sector(cluster_sector + s, sector_buffer);

                        for (int j = 0; j < bytes_to_read && bytes_read < (unsigned)max_size; j++) {
                            buffer[bytes_read++] = sector_buffer[j];
                        }

                        file_size -= bytes_to_read;
                    }

                    cluster = fat16_get_next_cluster((unsigned short)cluster);
                }

                return bytes_read;
            }
        }

        return 0;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            disk_read_sector(sector, sector_buffer);
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;

                if (!fat16_match_name(filename, &entries[i])) continue;

                unsigned int file_size = entries[i].file_size;
                unsigned int file_cluster = entries[i].cluster_low;
                unsigned int bytes_read = 0;

                while (file_cluster != 0xFFFF && bytes_read < (unsigned)max_size && file_size > 0) {
                    int cluster_sector = data_start + (file_cluster - 2) * bpb.sectors_per_cluster;

                    for (int cs = 0; cs < bpb.sectors_per_cluster && file_size > 0; cs++) {
                        int bytes_to_read = file_size > 512 ? 512 : file_size;
                        disk_read_sector(cluster_sector + cs, sector_buffer);

                        for (int j = 0; j < bytes_to_read && bytes_read < (unsigned)max_size; j++) {
                            buffer[bytes_read++] = sector_buffer[j];
                        }

                        file_size -= bytes_to_read;
                    }

                    file_cluster = fat16_get_next_cluster((unsigned short)file_cluster);
                }

                return bytes_read;
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    return 0;
}

int fat16_find_dir_cluster(unsigned int dir_cluster, const char* name, unsigned int* out_cluster) {
    if (!out_cluster) return 0;

    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = ((bpb.root_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;

        for (int sector = 0; sector < root_sectors; sector++) {
            disk_read_sector(root_start + sector, sector_buffer);
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                if (!(entries[i].attributes & FAT16_ATTR_DIRECTORY)) continue;

                if (!fat16_match_name(name, &entries[i])) continue;

                *out_cluster = entries[i].cluster_low;
                return 1;
            }
        }

        return 0;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            disk_read_sector(sector, sector_buffer);
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                if (!(entries[i].attributes & FAT16_ATTR_DIRECTORY)) continue;

                if (!fat16_match_name(name, &entries[i])) continue;

                *out_cluster = entries[i].cluster_low;
                return 1;
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    return 0;
}

int fat16_get_parent_cluster(unsigned int dir_cluster, unsigned int* out_cluster) {
    if (!out_cluster) return 0;
    if (dir_cluster == 0) {
        *out_cluster = 0;
        return 1;
    }

    return fat16_find_dir_cluster(dir_cluster, "..", out_cluster);
}

static int fat16_format_name(const char* input, unsigned char out_name[11]) {
    const char* invalid_chars = "\"*/:<>?\\|+;,=[]";

    for (int i = 0; i < 11; i++) {
        out_name[i] = ' ';
    }

    if (input[0] == '\0') return 0;
    if (input[0] == '.' && input[1] == '\0') return 0;
    if (input[0] == '.' && input[1] == '.' && input[2] == '\0') return 0;

    int i = 0;
    int name_pos = 0;
    while (input[i] && input[i] != '.') {
        if (name_pos >= 8) return 0;
        char c = input[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c == ' ') return 0;
        for (int k = 0; invalid_chars[k] != '\0'; k++) {
            if (c == invalid_chars[k]) return 0;
        }
        out_name[name_pos++] = (unsigned char)c;
        i++;
    }

    if (input[i] == '.') {
        i++;
        int ext_pos = 8;
        while (input[i]) {
            if (ext_pos >= 11) return 0;
            char c = input[i];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            if (c == ' ') return 0;
            for (int k = 0; invalid_chars[k] != '\0'; k++) {
                if (c == invalid_chars[k]) return 0;
            }
            out_name[ext_pos++] = (unsigned char)c;
            i++;
        }
    }

    return 1;
}

static int fat16_find_free_root_entry(int* out_sector, int* out_index) {
    int root_start = get_root_dir_start();
    int root_sectors = get_root_dir_sectors();

    for (int sector = 0; sector < root_sectors; sector++) {
        if (disk_read_sector(root_start + sector, sector_buffer) != 0) {
            return 0;
        }
        FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
        for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                *out_sector = root_start + sector;
                *out_index = (int)i;
                return 1;
            }
        }
    }

    return 0;
}

static int fat16_write_dir_entry_at(int sector, int index, const FAT16_DirectoryEntry* entry) {
    if (disk_read_sector(sector, sector_buffer) != 0) {
        return 0;
    }
    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    entries[index] = *entry;
    if (disk_write_sector(sector, sector_buffer) != 0) {
        return 0;
    }
    return 1;
}

static int fat16_alloc_cluster_internal(unsigned short* out_cluster) {
    if (!out_cluster) return 0;
    unsigned int total_clusters = get_total_clusters();
    unsigned int max_cluster = total_clusters + 1;

    for (unsigned int cluster = 2; cluster <= max_cluster; cluster++) {
        if (fat16_read_fat_entry((unsigned short)cluster) == 0x0000) {
            if (!fat16_write_fat_entry((unsigned short)cluster, 0xFFFF)) {
                return 0;
            }
            *out_cluster = (unsigned short)cluster;
            return 1;
        }
    }

    return 0;
}

static int fat16_zero_cluster(unsigned short cluster) {
    int data_start = get_data_start();
    int cluster_sector = data_start + (cluster - 2) * bpb.sectors_per_cluster;

    for (int s = 0; s < bpb.sectors_per_cluster; s++) {
        for (int i = 0; i < SECTOR_SIZE; i++) {
            sector_buffer[i] = 0;
        }
        if (disk_write_sector(cluster_sector + s, sector_buffer) != 0) {
            return 0;
        }
    }

    return 1;
}

static int fat16_init_dir_entries(unsigned short dir_cluster, unsigned int parent_cluster) {
    int data_start = get_data_start();
    int first_sector = data_start + (dir_cluster - 2) * bpb.sectors_per_cluster;

    if (disk_read_sector(first_sector, sector_buffer) != 0) {
        return 0;
    }

    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    FAT16_DirectoryEntry dot;
    FAT16_DirectoryEntry dotdot;

    for (int i = 0; i < 11; i++) {
        dot.name[i] = ' ';
        dotdot.name[i] = ' ';
    }
    dot.name[0] = '.';
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';

    dot.attributes = FAT16_ATTR_DIRECTORY;
    dotdot.attributes = FAT16_ATTR_DIRECTORY;
    dot.reserved = 0;
    dotdot.reserved = 0;
    dot.creation_time_ms = 0;
    dotdot.creation_time_ms = 0;
    dot.creation_time = 0;
    dotdot.creation_time = 0;
    dot.creation_date = 0;
    dotdot.creation_date = 0;
    dot.access_date = 0;
    dotdot.access_date = 0;
    dot.cluster_high = 0;
    dotdot.cluster_high = 0;
    dot.modified_time = 0;
    dotdot.modified_time = 0;
    dot.modified_date = 0;
    dotdot.modified_date = 0;
    dot.cluster_low = dir_cluster;
    dotdot.cluster_low = (unsigned short)parent_cluster;
    dot.file_size = 0;
    dotdot.file_size = 0;

    entries[0] = dot;
    entries[1] = dotdot;

    if (disk_write_sector(first_sector, sector_buffer) != 0) {
        return 0;
    }

    return 1;
}

static int fat16_add_dir_entry_internal(unsigned int dir_cluster, const unsigned char name_83[11], unsigned short cluster, unsigned int file_size, unsigned char attributes) {
    FAT16_DirectoryEntry entry;
    for (int i = 0; i < 11; i++) {
        entry.name[i] = name_83[i];
    }
    entry.attributes = attributes;
    entry.reserved = 0;
    entry.creation_time_ms = 0;
    entry.creation_time = 0;
    entry.creation_date = 0;
    entry.access_date = 0;
    entry.cluster_high = 0;
    entry.modified_time = 0;
    entry.modified_date = 0;
    entry.cluster_low = cluster;
    entry.file_size = file_size;

    if (dir_cluster == 0) {
        int sector = 0;
        int index = 0;
        if (!fat16_find_free_root_entry(&sector, &index)) {
            return 0;
        }
        return fat16_write_dir_entry_at(sector, index, &entry);
    }

    unsigned int cluster_iter = dir_cluster;
    int data_start = get_data_start();

    while (cluster_iter >= 2 && cluster_iter < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster_iter - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                return 0;
            }
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    entries[i] = entry;
                    if (disk_write_sector(sector, sector_buffer) != 0) {
                        return 0;
                    }
                    return 1;
                }
            }
        }

        unsigned short next = fat16_get_next_cluster((unsigned short)cluster_iter);
        if (next >= 0xFFF8) {
            break;
        }
        cluster_iter = next;
    }

    unsigned short new_cluster = 0;
    if (!fat16_alloc_cluster_internal(&new_cluster)) {
        return 0;
    }

    if (!fat16_write_fat_entry((unsigned short)cluster_iter, new_cluster)) {
        return 0;
    }
    if (!fat16_write_fat_entry(new_cluster, 0xFFFF)) {
        return 0;
    }

    if (!fat16_zero_cluster(new_cluster)) {
        return 0;
    }

    int first_sector = data_start + (new_cluster - 2) * bpb.sectors_per_cluster;
    if (disk_read_sector(first_sector, sector_buffer) != 0) {
        return 0;
    }
    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    entries[0] = entry;
    if (disk_write_sector(first_sector, sector_buffer) != 0) {
        return 0;
    }

    return 1;
}

static int fat16_find_entry_internal(
    unsigned int dir_cluster,
    const char* name,
    FAT16_DirectoryEntry* out_entry,
    int* out_sector,
    int* out_index
) {
    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = get_root_dir_sectors();

        for (int sector = 0; sector < root_sectors; sector++) {
            int abs_sector = root_start + sector;
            if (disk_read_sector(abs_sector, sector_buffer) != 0) {
                return 0;
            }

            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                if (!fat16_match_name(name, &entries[i])) continue;

                if (out_entry) *out_entry = entries[i];
                if (out_sector) *out_sector = abs_sector;
                if (out_index) *out_index = (int)i;
                return 1;
            }
        }

        return 0;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                return 0;
            }

            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                if (!fat16_match_name(name, &entries[i])) continue;

                if (out_entry) *out_entry = entries[i];
                if (out_sector) *out_sector = sector;
                if (out_index) *out_index = (int)i;
                return 1;
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    return 0;
}

static int fat16_is_directory_empty(unsigned short dir_cluster) {
    if (dir_cluster < 2) {
        return 0;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                return 0;
            }

            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) {
                    return 1;
                }
                if (entries[i].name[0] == 0xE5) continue;
                if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if (fat16_match_name(".", &entries[i])) continue;
                if (fat16_match_name("..", &entries[i])) continue;

                return 0;
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    return 1;
}

static int fat16_free_cluster_chain_internal(unsigned short start_cluster) {
    if (start_cluster < 2) {
        return 0;
    }

    unsigned short cluster = start_cluster;
    unsigned int max_steps = get_total_clusters() + 2;

    while (cluster >= 2 && cluster < 0xFFF8 && max_steps-- > 0) {
        unsigned short next = fat16_get_next_cluster(cluster);
        if (!fat16_write_fat_entry(cluster, 0x0000)) {
            return 0;
        }
        if (next == cluster) {
            break;
        }
        cluster = next;
    }

    return 1;
}

int fat16_find_entry(unsigned int dir_cluster, const char* name, FAT16_DirectoryEntry* out_entry, int* out_sector, int* out_index) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    return fat16_find_entry_internal(dir_cluster, name, out_entry, out_sector, out_index);
}

int fat16_create_entry(unsigned int dir_cluster, const char* name, unsigned short first_cluster, unsigned int file_size, unsigned char attributes) {
    unsigned char name_83[11];
    FAT16_DirectoryEntry existing;

    if (!name || name[0] == '\0') {
        return 0;
    }
    if (!fat16_format_name(name, name_83)) {
        return 0;
    }
    if (fat16_find_entry_internal(dir_cluster, name, &existing, 0, 0)) {
        return 0;
    }

    return fat16_add_dir_entry_internal(dir_cluster, name_83, first_cluster, file_size, attributes);
}

int fat16_update_entry(unsigned int dir_cluster, const char* old_name, const char* new_name, unsigned short first_cluster, unsigned int file_size, unsigned char attributes) {
    FAT16_DirectoryEntry current_entry;
    FAT16_DirectoryEntry conflict_entry;
    unsigned char name_83[11];
    int sector = 0;
    int index = 0;
    int same_name = 1;

    if (!old_name || !new_name || old_name[0] == '\0' || new_name[0] == '\0') {
        return 0;
    }
    if (!fat16_format_name(new_name, name_83)) {
        return 0;
    }
    if (!fat16_find_entry_internal(dir_cluster, old_name, &current_entry, &sector, &index)) {
        return 0;
    }

    for (int i = 0; i < 11; i++) {
        if (current_entry.name[i] != name_83[i]) {
            same_name = 0;
            break;
        }
    }
    if (!same_name && fat16_find_entry_internal(dir_cluster, new_name, &conflict_entry, 0, 0)) {
        return 0;
    }

    if (disk_read_sector(sector, sector_buffer) != 0) {
        return 0;
    }

    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    for (int i = 0; i < 11; i++) {
        entries[index].name[i] = name_83[i];
    }
    entries[index].attributes = attributes;
    entries[index].cluster_high = 0;
    entries[index].cluster_low = first_cluster;
    entries[index].file_size = file_size;

    return disk_write_sector(sector, sector_buffer) == 0;
}

int fat16_delete_entry(unsigned int dir_cluster, const char* name, int free_cluster_chain) {
    FAT16_DirectoryEntry target;
    int sector = 0;
    int index = 0;

    if (!name || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '.' && name[1] == '\0') {
        return 0;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        return 0;
    }
    if (!fat16_find_entry_internal(dir_cluster, name, &target, &sector, &index)) {
        return 0;
    }
    if (target.attributes & FAT16_ATTR_DIRECTORY) {
        return 0;
    }
    if (free_cluster_chain && target.cluster_low >= 2) {
        if (!fat16_free_cluster_chain_internal(target.cluster_low)) {
            return 0;
        }
    }

    if (disk_read_sector(sector, sector_buffer) != 0) {
        return 0;
    }
    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    entries[index].name[0] = 0xE5;

    return disk_write_sector(sector, sector_buffer) == 0;
}

int fat16_delete_matching(unsigned int dir_cluster, const char* pattern, int free_cluster_chain, int* deleted_count) {
    int deleted = 0;

    if (!pattern || pattern[0] == '\0') {
        if (deleted_count) *deleted_count = 0;
        return 0;
    }

    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = get_root_dir_sectors();

        for (int sector = 0; sector < root_sectors; sector++) {
            int abs_sector = root_start + sector;
            if (disk_read_sector(abs_sector, sector_buffer) != 0) {
                return 0;
            }

            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) {
                    if (deleted_count) *deleted_count = deleted;
                    return 1;
                }
                if (!fat16_entry_is_visible(&entries[i])) continue;
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue;
                if (!fat16_match_pattern(pattern, &entries[i])) continue;

                if (free_cluster_chain && entries[i].cluster_low >= 2) {
                    if (!fat16_free_cluster_chain_internal(entries[i].cluster_low)) {
                        return 0;
                    }
                }
                entries[i].name[0] = 0xE5;
                deleted++;
            }

            if (disk_write_sector(abs_sector, sector_buffer) != 0) {
                return 0;
            }
        }

        if (deleted_count) *deleted_count = deleted;
        return 1;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();

    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                return 0;
            }

            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) {
                    if (deleted_count) *deleted_count = deleted;
                    return 1;
                }
                if (!fat16_entry_is_visible(&entries[i])) continue;
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue;
                if (!fat16_match_pattern(pattern, &entries[i])) continue;

                if (free_cluster_chain && entries[i].cluster_low >= 2) {
                    if (!fat16_free_cluster_chain_internal(entries[i].cluster_low)) {
                        return 0;
                    }
                }
                entries[i].name[0] = 0xE5;
                deleted++;
            }

            if (disk_write_sector(sector, sector_buffer) != 0) {
                return 0;
            }
        }

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    if (deleted_count) *deleted_count = deleted;
    return 1;
}

int fat16_copy_file_between_dirs(unsigned int src_dir_cluster, const char* src_name, unsigned int dst_dir_cluster, const char* dst_name) {
    FAT16_DirectoryEntry src_entry;
    FAT16_DirectoryEntry dst_entry;
    unsigned char src_83[11];
    unsigned char dst_83[11];
    unsigned int remaining = 0;
    unsigned int max_steps = 0;
    unsigned short src_cluster = 0;
    unsigned short dst_first_cluster = 0;
    unsigned short dst_cluster = 0;
    int data_start = get_data_start();

    if (!src_name || !dst_name || src_name[0] == '\0' || dst_name[0] == '\0') {
        return 0;
    }
    if (!fat16_format_name(src_name, src_83) || !fat16_format_name(dst_name, dst_83)) {
        return 0;
    }

    int same_name = 1;
    for (int i = 0; i < 11; i++) {
        if (src_83[i] != dst_83[i]) {
            same_name = 0;
            break;
        }
    }
    if (same_name && src_dir_cluster == dst_dir_cluster) {
        return 0;
    }

    if (!fat16_find_entry_internal(src_dir_cluster, src_name, &src_entry, 0, 0)) {
        return 0;
    }
    if (src_entry.attributes & FAT16_ATTR_DIRECTORY) {
        return 0;
    }
    if (fat16_find_entry_internal(dst_dir_cluster, dst_name, &dst_entry, 0, 0)) {
        return 0;
    }

    remaining = src_entry.file_size;
    src_cluster = src_entry.cluster_low;
    max_steps = get_total_clusters() + 2;

    if (remaining > 0) {
        if (src_cluster < 2) {
            return 0;
        }
        if (!fat16_alloc_cluster_internal(&dst_first_cluster)) {
            return 0;
        }
        dst_cluster = dst_first_cluster;
    }

    while (remaining > 0 && max_steps-- > 0) {
        int src_sector_base = data_start + (src_cluster - 2) * bpb.sectors_per_cluster;
        int dst_sector_base = data_start + (dst_cluster - 2) * bpb.sectors_per_cluster;

        for (int s = 0; s < bpb.sectors_per_cluster && remaining > 0; s++) {
            unsigned int bytes_this_sector = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;

            if (disk_read_sector(src_sector_base + s, sector_buffer) != 0) {
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }
            if (disk_write_sector(dst_sector_base + s, sector_buffer) != 0) {
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }

            remaining -= bytes_this_sector;
        }

        if (remaining > 0) {
            unsigned short next_src = fat16_get_next_cluster(src_cluster);
            unsigned short next_dst = 0;

            if (next_src < 2 || next_src >= 0xFFF8) {
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }
            if (!fat16_alloc_cluster_internal(&next_dst)) {
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }
            if (!fat16_write_fat_entry(dst_cluster, next_dst)) {
                fat16_free_cluster_chain_internal(next_dst);
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }
            if (!fat16_write_fat_entry(next_dst, 0xFFFF)) {
                fat16_free_cluster_chain_internal(next_dst);
                if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
                return 0;
            }

            src_cluster = next_src;
            dst_cluster = next_dst;
        }
    }

    if (remaining != 0) {
        if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
        return 0;
    }

    if (!fat16_create_entry(dst_dir_cluster, dst_name, dst_first_cluster, src_entry.file_size, src_entry.attributes)) {
        if (dst_first_cluster >= 2) fat16_free_cluster_chain_internal(dst_first_cluster);
        return 0;
    }

    return 1;
}

int fat16_copy_file(unsigned int dir_cluster, const char* src_name, const char* dst_name) {
    return fat16_copy_file_between_dirs(dir_cluster, src_name, dir_cluster, dst_name);
}

int fat16_get_entry_by_index(unsigned int dir_cluster, unsigned int index, char* out_name, int out_name_size, int* out_is_dir, unsigned int* out_file_size) {
    unsigned int current = 0;
    FAT16_DirectoryEntry entry;

    if (!out_name || out_name_size < 2) {
        return 0;
    }
    out_name[0] = '\0';
    if (out_is_dir) {
        *out_is_dir = 0;
    }
    if (out_file_size) {
        *out_file_size = 0;
    }

    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = get_root_dir_sectors();

        for (int sector = 0; sector < root_sectors; sector++) {
            if (disk_read_sector(root_start + sector, sector_buffer) != 0) {
                return 0;
            }
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) {
                    return 0;
                }
                if (!fat16_entry_is_visible(&entries[i])) {
                    continue;
                }

                if (current == index) {
                    entry = entries[i];
                    fat16_build_display_name(&entry, out_name);
                    if (out_is_dir) {
                        *out_is_dir = (entry.attributes & FAT16_ATTR_DIRECTORY) ? 1 : 0;
                    }
                    if (out_file_size) {
                        *out_file_size = entry.file_size;
                    }
                    return 1;
                }
                current++;
            }
        }
        return 0;
    }

    unsigned int cluster = dir_cluster;
    int data_start = get_data_start();
    while (cluster >= 2 && cluster < 0xFFF8) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                return 0;
            }
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;

            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00) {
                    return 0;
                }
                if (!fat16_entry_is_visible(&entries[i])) {
                    continue;
                }

                if (current == index) {
                    entry = entries[i];
                    fat16_build_display_name(&entry, out_name);
                    if (out_is_dir) {
                        *out_is_dir = (entry.attributes & FAT16_ATTR_DIRECTORY) ? 1 : 0;
                    }
                    if (out_file_size) {
                        *out_file_size = entry.file_size;
                    }
                    return 1;
                }
                current++;
            }
        }
        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }

    return 0;
}

int fat16_alloc_cluster(unsigned short* out_cluster) {
    return fat16_alloc_cluster_internal(out_cluster);
}

int fat16_free_cluster_chain(unsigned short start_cluster) {
    return fat16_free_cluster_chain_internal(start_cluster);
}

int fat16_mkdir(unsigned int dir_cluster, const char* name) {
    unsigned char name_83[11];
    if (!fat16_format_name(name, name_83)) {
        return 0;
    }

    unsigned int existing_cluster = 0;
    if (fat16_find_dir_cluster(dir_cluster, name, &existing_cluster)) {
        return 0;
    }

    unsigned short new_cluster = 0;
    if (!fat16_alloc_cluster_internal(&new_cluster)) {
        return 0;
    }

    if (!fat16_zero_cluster(new_cluster)) {
        return 0;
    }

    if (!fat16_init_dir_entries(new_cluster, dir_cluster)) {
        return 0;
    }

    if (!fat16_add_dir_entry_internal(dir_cluster, name_83, new_cluster, 0, FAT16_ATTR_DIRECTORY)) {
        return 0;
    }

    return 1;
}

int fat16_rmdir(unsigned int dir_cluster, const char* name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '.' && name[1] == '\0') {
        return 0;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        return 0;
    }

    FAT16_DirectoryEntry target;
    int sector = 0;
    int index = 0;
    if (!fat16_find_entry_internal(dir_cluster, name, &target, &sector, &index)) {
        return 0;
    }
    if (!(target.attributes & FAT16_ATTR_DIRECTORY)) {
        return 0;
    }
    if (target.cluster_low < 2) {
        return 0;
    }

    if (!fat16_is_directory_empty(target.cluster_low)) {
        return 0;
    }

    if (!fat16_free_cluster_chain_internal(target.cluster_low)) {
        return 0;
    }

    if (disk_read_sector(sector, sector_buffer) != 0) {
        return 0;
    }
    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
    entries[index].name[0] = 0xE5;
    if (disk_write_sector(sector, sector_buffer) != 0) {
        return 0;
    }

    return 1;
}

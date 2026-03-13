#include "fat16_internal.h"
#include "serial.h"
#include "video.h"

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
        print_both_char((char)entry->name[j]);
    }

    if (ext_present) {
        print_both_char('.');
        for (int j = 8; j < 11; j++) {
            print_both_char((char)entry->name[j]);
        }
    } else {
        print_both_string("    ");
    }
}

int fat16_match_name(const char* filename, const FAT16_DirectoryEntry* entry) {
    int match = 1;
    int name_len = 0;
    int dir_name_end = 8;

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

    for (int j = 0; j < 8; j++) {
        if (filename[name_len] == '.' || filename[name_len] == '\0') break;
        if (entry->name[j] != (unsigned char)filename[name_len]) {
            match = 0;
            break;
        }
        name_len++;
    }

    if (!match) return 0;

    while (dir_name_end > 0 && entry->name[dir_name_end - 1] == ' ') dir_name_end--;
    if (name_len != dir_name_end) {
        return 0;
    }

    {
        if (filename[name_len] == '.') {
            int ext_pos;

            name_len++;
            ext_pos = 8;
            while (filename[name_len] && ext_pos < 11) {
                if (entry->name[ext_pos] != (unsigned char)filename[name_len]) {
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
        const char* t;

        while (*(pattern + 1) == '*') {
            pattern++;
        }
        if (*(pattern + 1) == '\0') {
            return 1;
        }

        t = text;
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

void fat16_list_root(void) {
    fat16_list_root_filtered(0);
}

void fat16_list_root_filtered(const char* pattern) {
    int root_start = get_root_dir_start();
    int root_sectors = get_root_dir_sectors();

    for (int sector = 0; sector < root_sectors; sector++) {
        if (disk_read_sector(root_start + sector, sector_buffer) != 0) {
            print_both_string("[FAT16] Error reading root directory\n");
            return;
        }

        {
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
}

void fat16_list_dir(unsigned int dir_cluster) {
    fat16_list_dir_filtered(dir_cluster, 0);
}

void fat16_list_dir_filtered(unsigned int dir_cluster, const char* pattern) {
    unsigned int cluster;
    int data_start;

    if (dir_cluster == 0) {
        fat16_list_root_filtered(pattern);
        return;
    }

    cluster = dir_cluster;
    data_start = get_data_start();

    while (fat_is_data_cluster(cluster)) {
        for (int s = 0; s < bpb.sectors_per_cluster; s++) {
            int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
            if (disk_read_sector(sector, sector_buffer) != 0) {
                print_both_string("[FAT16] Error reading directory\n");
                return;
            }

            {
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

        cluster = fat16_get_next_cluster((unsigned short)cluster);
    }
}

int fat16_find_dir_cluster(unsigned int dir_cluster, const char* name, unsigned int* out_cluster) {
    FAT16_DirectoryEntry entry;

    if (!out_cluster || !fat16_find_entry_internal(dir_cluster, name, &entry, 0, 0)) {
        return 0;
    }
    if (!(entry.attributes & FAT16_ATTR_DIRECTORY)) {
        return 0;
    }

    *out_cluster = entry.cluster_low;
    return 1;
}

int fat16_get_parent_cluster(unsigned int dir_cluster, unsigned int* out_cluster) {
    if (!out_cluster) return 0;
    if (dir_cluster == 0) {
        *out_cluster = 0;
        return 1;
    }

    return fat16_find_dir_cluster(dir_cluster, "..", out_cluster);
}

int fat16_format_name(const char* input, unsigned char out_name[11]) {
    const char* invalid_chars = "\"*/:<>?\\|+;,=[]";
    int i = 0;
    int name_pos = 0;

    for (int j = 0; j < 11; j++) {
        out_name[j] = ' ';
    }

    if (input[0] == '\0') return 0;
    if (input[0] == '.' && input[1] == '\0') return 0;
    if (input[0] == '.' && input[1] == '.' && input[2] == '\0') return 0;

    while (input[i] && input[i] != '.') {
        char c = input[i];

        if (name_pos >= 8) return 0;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == ' ') return 0;
        for (int k = 0; invalid_chars[k] != '\0'; k++) {
            if (c == invalid_chars[k]) return 0;
        }
        out_name[name_pos++] = (unsigned char)c;
        i++;
    }

    if (input[i] == '.') {
        int ext_pos = 8;
        i++;
        while (input[i]) {
            char c = input[i];

            if (ext_pos >= 11) return 0;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
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

        {
            FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
            for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    *out_sector = root_start + sector;
                    *out_index = (int)i;
                    return 1;
                }
            }
        }
    }

    return 0;
}

static int fat16_write_dir_entry_at(int sector, int index, const FAT16_DirectoryEntry* entry) {
    if (disk_read_sector(sector, sector_buffer) != 0) {
        return 0;
    }

    ((FAT16_DirectoryEntry*)sector_buffer)[index] = *entry;
    return disk_write_sector(sector, sector_buffer) == 0;
}

static int fat16_init_dir_entries(unsigned short dir_cluster, unsigned int parent_cluster) {
    int first_sector = get_data_start() + (dir_cluster - 2) * bpb.sectors_per_cluster;
    FAT16_DirectoryEntry dot;
    FAT16_DirectoryEntry dotdot;

    if (disk_read_sector(first_sector, sector_buffer) != 0) {
        return 0;
    }

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

    ((FAT16_DirectoryEntry*)sector_buffer)[0] = dot;
    ((FAT16_DirectoryEntry*)sector_buffer)[1] = dotdot;

    return disk_write_sector(first_sector, sector_buffer) == 0;
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

    {
        unsigned int cluster_iter = dir_cluster;
        int data_start = get_data_start();

        while (fat_is_data_cluster(cluster_iter)) {
            for (int s = 0; s < bpb.sectors_per_cluster; s++) {
                int sector = data_start + (cluster_iter - 2) * bpb.sectors_per_cluster + s;
                if (disk_read_sector(sector, sector_buffer) != 0) {
                    return 0;
                }

                {
                    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
                    for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                            entries[i] = entry;
                            return disk_write_sector(sector, sector_buffer) == 0;
                        }
                    }
                }
            }

            {
                unsigned short next = fat16_get_next_cluster((unsigned short)cluster_iter);
                if (!fat_is_data_cluster(next)) {
                    break;
                }
                cluster_iter = next;
            }
        }

        {
            unsigned short new_cluster = 0;
            int first_sector;

            if (!fat16_alloc_cluster_internal(&new_cluster)) {
                return 0;
            }
            if (!fat16_write_fat_entry((unsigned short)cluster_iter, new_cluster)) {
                return 0;
            }
            if (!fat16_write_fat_entry(new_cluster, fat_end_of_chain_value())) {
                return 0;
            }
            if (!fat16_zero_cluster(new_cluster)) {
                return 0;
            }

            first_sector = data_start + (new_cluster - 2) * bpb.sectors_per_cluster;
            if (disk_read_sector(first_sector, sector_buffer) != 0) {
                return 0;
            }
            ((FAT16_DirectoryEntry*)sector_buffer)[0] = entry;
            return disk_write_sector(first_sector, sector_buffer) == 0;
        }
    }
}

int fat16_find_entry_internal(
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

            {
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
        }

        return 0;
    }

    {
        unsigned int cluster = dir_cluster;
        int data_start = get_data_start();

        while (fat_is_data_cluster(cluster)) {
            for (int s = 0; s < bpb.sectors_per_cluster; s++) {
                int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
                if (disk_read_sector(sector, sector_buffer) != 0) {
                    return 0;
                }

                {
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
            }

            cluster = fat16_get_next_cluster((unsigned short)cluster);
        }
    }

    return 0;
}

static int fat16_is_directory_empty(unsigned short dir_cluster) {
    if (dir_cluster < 2) {
        return 0;
    }

    {
        unsigned int cluster = dir_cluster;
        int data_start = get_data_start();

        while (fat_is_data_cluster(cluster)) {
            for (int s = 0; s < bpb.sectors_per_cluster; s++) {
                int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
                if (disk_read_sector(sector, sector_buffer) != 0) {
                    return 0;
                }

                {
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
            }

            cluster = fat16_get_next_cluster((unsigned short)cluster);
        }
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

    {
        FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
        for (int i = 0; i < 11; i++) {
            entries[index].name[i] = name_83[i];
        }
        entries[index].attributes = attributes;
        entries[index].cluster_high = 0;
        entries[index].cluster_low = first_cluster;
        entries[index].file_size = file_size;
    }

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

    ((FAT16_DirectoryEntry*)sector_buffer)[index].name[0] = 0xE5;
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

            {
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
            }

            if (disk_write_sector(abs_sector, sector_buffer) != 0) {
                return 0;
            }
        }

        if (deleted_count) *deleted_count = deleted;
        return 1;
    }

    {
        unsigned int cluster = dir_cluster;
        int data_start = get_data_start();

        while (fat_is_data_cluster(cluster)) {
            for (int s = 0; s < bpb.sectors_per_cluster; s++) {
                int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
                if (disk_read_sector(sector, sector_buffer) != 0) {
                    return 0;
                }

                {
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
                }

                if (disk_write_sector(sector, sector_buffer) != 0) {
                    return 0;
                }
            }

            cluster = fat16_get_next_cluster((unsigned short)cluster);
        }
    }

    if (deleted_count) *deleted_count = deleted;
    return 1;
}

int fat16_get_entry_by_index(unsigned int dir_cluster, unsigned int index, char* out_name, int out_name_size, int* out_is_dir, unsigned int* out_file_size) {
    unsigned int current = 0;

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

            {
                FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
                for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                    if (entries[i].name[0] == 0x00) {
                        return 0;
                    }
                    if (!fat16_entry_is_visible(&entries[i])) {
                        continue;
                    }
                    if (current == index) {
                        fat16_build_display_name(&entries[i], out_name);
                        if (out_is_dir) {
                            *out_is_dir = (entries[i].attributes & FAT16_ATTR_DIRECTORY) ? 1 : 0;
                        }
                        if (out_file_size) {
                            *out_file_size = entries[i].file_size;
                        }
                        return 1;
                    }
                    current++;
                }
            }
        }
        return 0;
    }

    {
        unsigned int cluster = dir_cluster;
        int data_start = get_data_start();

        while (fat_is_data_cluster(cluster)) {
            for (int s = 0; s < bpb.sectors_per_cluster; s++) {
                int sector = data_start + (cluster - 2) * bpb.sectors_per_cluster + s;
                if (disk_read_sector(sector, sector_buffer) != 0) {
                    return 0;
                }

                {
                    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
                    for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                        if (entries[i].name[0] == 0x00) {
                            return 0;
                        }
                        if (!fat16_entry_is_visible(&entries[i])) {
                            continue;
                        }
                        if (current == index) {
                            fat16_build_display_name(&entries[i], out_name);
                            if (out_is_dir) {
                                *out_is_dir = (entries[i].attributes & FAT16_ATTR_DIRECTORY) ? 1 : 0;
                            }
                            if (out_file_size) {
                                *out_file_size = entries[i].file_size;
                            }
                            return 1;
                        }
                        current++;
                    }
                }
            }
            cluster = fat16_get_next_cluster((unsigned short)cluster);
        }
    }

    return 0;
}

int fat16_mkdir(unsigned int dir_cluster, const char* name) {
    unsigned char name_83[11];
    unsigned int existing_cluster = 0;
    unsigned short new_cluster = 0;

    if (!fat16_format_name(name, name_83)) {
        return 0;
    }
    if (fat16_find_dir_cluster(dir_cluster, name, &existing_cluster)) {
        return 0;
    }
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

    ((FAT16_DirectoryEntry*)sector_buffer)[index].name[0] = 0xE5;
    return disk_write_sector(sector, sector_buffer) == 0;
}

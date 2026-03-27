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

int fat16_find_entry(unsigned int dir_cluster, const char* name, FAT16_DirectoryEntry* out_entry, int* out_sector, int* out_index) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    return fat16_find_entry_internal(dir_cluster, name, out_entry, out_sector, out_index);
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

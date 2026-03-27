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

#include "fat16_internal.h"

int fat16_write_file(const char* filename, const unsigned char* buffer, int size) {
    return fat16_write_file_from_dir(0, filename, buffer, size);
}

int fat16_write_file_from_dir(unsigned int dir_cluster, const char* filename, const unsigned char* buffer, int size) {
    FAT16_DirectoryEntry existing;
    unsigned short old_first_cluster = 0;
    unsigned short new_first_cluster = 0;
    unsigned short write_cluster = 0;
    unsigned short prev_cluster = 0;
    unsigned int remaining;
    unsigned int max_steps;
    int cluster_bytes = (int)bpb.sectors_per_cluster * SECTOR_SIZE;
    int offset = 0;
    int exists;

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
            if (!fat16_write_fat_entry(write_cluster, fat_end_of_chain_value())) {
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
        }
    }

    if (remaining != 0) {
        if (new_first_cluster >= 2) {
            fat16_free_cluster_chain_internal(new_first_cluster);
        }
        return 0;
    }

    if (exists) {
        if (!fat16_update_entry(
                dir_cluster,
                filename,
                filename,
                new_first_cluster,
                (unsigned int)size,
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

int fat16_read_file(const char* filename, unsigned char* buffer, int max_size) {
    return fat16_read_file_from_dir(0, filename, buffer, max_size);
}

int fat16_read_file_from_dir(unsigned int dir_cluster, const char* filename, unsigned char* buffer, int max_size) {
    if (dir_cluster == 0) {
        int root_start = get_root_dir_start();
        int root_sectors = get_root_dir_sectors();

        for (int sector = 0; sector < root_sectors; sector++) {
            disk_read_sector(root_start + sector, sector_buffer);
            {
                FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
                for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                    if (entries[i].name[0] == 0x00) return 0;
                    if (entries[i].name[0] == 0xE5) continue;
                    if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue;
                    if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                    if (!fat16_match_name(filename, &entries[i])) continue;

                    {
                        unsigned int file_size = entries[i].file_size;
                        unsigned int cluster = entries[i].cluster_low;
                        unsigned int bytes_read = 0;

                        while (fat_is_data_cluster(cluster) && bytes_read < (unsigned int)max_size && file_size > 0) {
                            int cluster_sector = get_data_start() + (cluster - 2) * bpb.sectors_per_cluster;

                            for (int s = 0; s < bpb.sectors_per_cluster && file_size > 0; s++) {
                                int bytes_to_read = file_size > 512 ? 512 : (int)file_size;
                                disk_read_sector(cluster_sector + s, sector_buffer);

                                for (int j = 0; j < bytes_to_read && bytes_read < (unsigned int)max_size; j++) {
                                    buffer[bytes_read++] = sector_buffer[j];
                                }

                                file_size -= (unsigned int)bytes_to_read;
                            }

                            cluster = fat16_get_next_cluster((unsigned short)cluster);
                        }

                        return (int)bytes_read;
                    }
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
                disk_read_sector(sector, sector_buffer);
                {
                    FAT16_DirectoryEntry* entries = (FAT16_DirectoryEntry*)sector_buffer;
                    for (unsigned int i = 0; i < SECTOR_SIZE / sizeof(FAT16_DirectoryEntry); i++) {
                        if (entries[i].name[0] == 0x00) return 0;
                        if (entries[i].name[0] == 0xE5) continue;
                        if (entries[i].attributes & FAT16_ATTR_DIRECTORY) continue;
                        if ((entries[i].attributes & 0x0F) == 0x0F) continue;
                        if (!fat16_match_name(filename, &entries[i])) continue;

                        {
                            unsigned int file_size = entries[i].file_size;
                            unsigned int file_cluster = entries[i].cluster_low;
                            unsigned int bytes_read = 0;

                            while (fat_is_data_cluster(file_cluster) && bytes_read < (unsigned int)max_size && file_size > 0) {
                                int cluster_sector = data_start + (file_cluster - 2) * bpb.sectors_per_cluster;

                                for (int cs = 0; cs < bpb.sectors_per_cluster && file_size > 0; cs++) {
                                    int bytes_to_read = file_size > 512 ? 512 : (int)file_size;
                                    disk_read_sector(cluster_sector + cs, sector_buffer);

                                    for (int j = 0; j < bytes_to_read && bytes_read < (unsigned int)max_size; j++) {
                                        buffer[bytes_read++] = sector_buffer[j];
                                    }

                                    file_size -= (unsigned int)bytes_to_read;
                                }

                                file_cluster = fat16_get_next_cluster((unsigned short)file_cluster);
                            }

                            return (int)bytes_read;
                        }
                    }
                }
            }

            cluster = fat16_get_next_cluster((unsigned short)cluster);
        }
    }

    return 0;
}

int fat16_copy_file_between_dirs(unsigned int src_dir_cluster, const char* src_name, unsigned int dst_dir_cluster, const char* dst_name) {
    FAT16_DirectoryEntry src_entry;
    FAT16_DirectoryEntry dst_entry;
    unsigned char src_83[11];
    unsigned char dst_83[11];
    unsigned int remaining;
    unsigned int max_steps;
    unsigned short src_cluster;
    unsigned short dst_first_cluster = 0;
    unsigned short dst_cluster = 0;
    int data_start = get_data_start();

    if (!src_name || !dst_name || src_name[0] == '\0' || dst_name[0] == '\0') {
        return 0;
    }
    if (!fat16_format_name(src_name, src_83) || !fat16_format_name(dst_name, dst_83)) {
        return 0;
    }

    {
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

            if (!fat_is_data_cluster(next_src)) {
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
            if (!fat16_write_fat_entry(next_dst, fat_end_of_chain_value())) {
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

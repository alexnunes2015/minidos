#include "fat16_internal.h"
#include "drive.h"
#include "serial.h"
#include "video.h"

FAT16_BPB bpb;
unsigned char sector_buffer[SECTOR_SIZE];
static unsigned char fat_sector_buffer_aux[SECTOR_SIZE];

static int current_drive_letter = 0;  // A:
static int fat16_debug_enabled = 0;
static unsigned int current_fat_type = FAT_TYPE_16;

static unsigned int get_total_sectors(void);
static unsigned short fat16_read_fat_entry(unsigned short cluster);

int disk_read_sector(int lba, unsigned char* buffer) {
    return drive_read_sector(current_drive_letter, (unsigned int)lba, buffer);
}

int disk_write_sector(int lba, unsigned char* buffer) {
    return drive_write_sector(current_drive_letter, (unsigned int)lba, buffer);
}

void fat16_set_drive(int drive_letter) {
    current_drive_letter = drive_letter;
}

int fat16_init() {
    unsigned int total_clusters;

    if (fat16_debug_enabled) {
        serial_print("[FAT16] init drive=");
        serial_putchar((char)('A' + current_drive_letter));
        serial_print(" lba=0\n");
    }

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

    {
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
    }

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

    total_clusters = get_total_clusters();
    if (total_clusters < 4085U) {
        current_fat_type = FAT_TYPE_12;
    } else if (total_clusters < 65525U) {
        current_fat_type = FAT_TYPE_16;
    } else {
        print_string("[FAT16] Warning: Unsupported FAT type\n");
        return 0;
    }

    if (fat16_debug_enabled) {
        serial_print("[FAT16] ok bps=");
        serial_print_hex(bpb.bytes_per_sector);
        serial_print(" spf=");
        serial_print_hex(bpb.sectors_per_fat);
        serial_print(" root=");
        serial_print_hex(bpb.root_entries);
        serial_print(" type=");
        serial_print(current_fat_type == FAT_TYPE_12 ? "12" : "16");
        serial_print("\n");
    }

    return 1;
}

int get_root_dir_start(void) {
    return bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat);
}

int get_data_start(void) {
    return get_root_dir_start() + get_root_dir_sectors();
}

int get_root_dir_sectors(void) {
    return ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
}

static unsigned int get_total_sectors(void) {
    if (bpb.total_sectors_16 != 0) {
        return bpb.total_sectors_16;
    }
    return bpb.total_sectors_32;
}

unsigned int get_total_clusters(void) {
    unsigned int data_sectors = get_total_sectors() - (bpb.reserved_sectors + (bpb.fat_count * bpb.sectors_per_fat) + get_root_dir_sectors());
    return data_sectors / bpb.sectors_per_cluster;
}

int fat_is_data_cluster(unsigned int cluster) {
    unsigned int max_cluster = get_total_clusters() + 1;
    return cluster >= 2 && cluster <= max_cluster;
}

unsigned short fat_end_of_chain_value(void) {
    return current_fat_type == FAT_TYPE_12 ? 0x0FFF : 0xFFFF;
}

static unsigned short fat16_read_fat_entry(unsigned short cluster) {
    if (current_fat_type == FAT_TYPE_12) {
        unsigned int fat_offset = cluster + (cluster / 2);
        unsigned int fat_sector = bpb.reserved_sectors + fat_offset / SECTOR_SIZE;
        unsigned int fat_byte_offset = fat_offset % SECTOR_SIZE;
        unsigned short pair;

        if (disk_read_sector((int)fat_sector, sector_buffer) != 0) {
            return fat_end_of_chain_value();
        }
        if (fat_byte_offset == (SECTOR_SIZE - 1)) {
            if (disk_read_sector((int)(fat_sector + 1), fat_sector_buffer_aux) != 0) {
                return fat_end_of_chain_value();
            }
            pair = (unsigned short)(sector_buffer[fat_byte_offset] | (fat_sector_buffer_aux[0] << 8));
        } else {
            pair = (unsigned short)(sector_buffer[fat_byte_offset] | (sector_buffer[fat_byte_offset + 1] << 8));
        }

        if (cluster & 1) {
            pair >>= 4;
        } else {
            pair &= 0x0FFF;
        }
        return (unsigned short)(pair & 0x0FFF);
    }

    {
        unsigned int fat_sector = bpb.reserved_sectors + (cluster * 2) / SECTOR_SIZE;
        unsigned int fat_offset = (cluster * 2) % SECTOR_SIZE;

        if (disk_read_sector((int)fat_sector, sector_buffer) != 0) {
            return fat_end_of_chain_value();
        }
        return *(unsigned short*)(sector_buffer + fat_offset);
    }
}

int fat16_write_fat_entry(unsigned short cluster, unsigned short value) {
    if (current_fat_type == FAT_TYPE_12) {
        unsigned short masked_value = (unsigned short)(value & 0x0FFF);
        unsigned int fat_offset = cluster + (cluster / 2);
        unsigned int fat_sector_index = fat_offset / SECTOR_SIZE;
        unsigned int fat_byte_offset = fat_offset % SECTOR_SIZE;

        for (unsigned int fat = 0; fat < bpb.fat_count; fat++) {
            unsigned int fat_sector = bpb.reserved_sectors + fat * bpb.sectors_per_fat + fat_sector_index;
            unsigned short pair;

            if (disk_read_sector((int)fat_sector, sector_buffer) != 0) {
                return 0;
            }

            if (fat_byte_offset == (SECTOR_SIZE - 1)) {
                if (disk_read_sector((int)(fat_sector + 1), fat_sector_buffer_aux) != 0) {
                    return 0;
                }
                pair = (unsigned short)(sector_buffer[fat_byte_offset] | (fat_sector_buffer_aux[0] << 8));
                if (cluster & 1) {
                    pair = (unsigned short)((pair & 0x000F) | (masked_value << 4));
                } else {
                    pair = (unsigned short)((pair & 0xF000) | masked_value);
                }
                sector_buffer[fat_byte_offset] = (unsigned char)(pair & 0xFF);
                fat_sector_buffer_aux[0] = (unsigned char)((pair >> 8) & 0xFF);
                if (disk_write_sector((int)fat_sector, sector_buffer) != 0) {
                    return 0;
                }
                if (disk_write_sector((int)(fat_sector + 1), fat_sector_buffer_aux) != 0) {
                    return 0;
                }
            } else {
                pair = (unsigned short)(sector_buffer[fat_byte_offset] | (sector_buffer[fat_byte_offset + 1] << 8));
                if (cluster & 1) {
                    pair = (unsigned short)((pair & 0x000F) | (masked_value << 4));
                } else {
                    pair = (unsigned short)((pair & 0xF000) | masked_value);
                }
                sector_buffer[fat_byte_offset] = (unsigned char)(pair & 0xFF);
                sector_buffer[fat_byte_offset + 1] = (unsigned char)((pair >> 8) & 0xFF);
                if (disk_write_sector((int)fat_sector, sector_buffer) != 0) {
                    return 0;
                }
            }
        }

        return 1;
    }

    {
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
    }

    return 1;
}

unsigned short fat16_get_next_cluster(unsigned short cluster) {
    return fat16_read_fat_entry(cluster);
}

int fat16_alloc_cluster_internal(unsigned short* out_cluster) {
    unsigned int total_clusters;
    unsigned int max_cluster;

    if (!out_cluster) return 0;

    total_clusters = get_total_clusters();
    max_cluster = total_clusters + 1;

    for (unsigned int cluster = 2; cluster <= max_cluster; cluster++) {
        if (fat16_read_fat_entry((unsigned short)cluster) == 0x0000) {
            if (!fat16_write_fat_entry((unsigned short)cluster, fat_end_of_chain_value())) {
                return 0;
            }
            *out_cluster = (unsigned short)cluster;
            return 1;
        }
    }

    return 0;
}

int fat16_zero_cluster(unsigned short cluster) {
    int cluster_sector = get_data_start() + (cluster - 2) * bpb.sectors_per_cluster;

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

int fat16_free_cluster_chain_internal(unsigned short start_cluster) {
    unsigned short cluster;
    unsigned int max_steps;

    if (start_cluster < 2) {
        return 0;
    }

    cluster = start_cluster;
    max_steps = get_total_clusters() + 2;

    while (fat_is_data_cluster(cluster) && max_steps-- > 0) {
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

int fat16_alloc_cluster(unsigned short* out_cluster) {
    return fat16_alloc_cluster_internal(out_cluster);
}

int fat16_free_cluster_chain(unsigned short start_cluster) {
    return fat16_free_cluster_chain_internal(start_cluster);
}

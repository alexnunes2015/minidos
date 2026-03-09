#ifndef FAT16_INTERNAL_H
#define FAT16_INTERNAL_H

#include "fat16.h"

#define FAT_TYPE_12 12
#define FAT_TYPE_16 16

extern FAT16_BPB bpb;
extern unsigned char sector_buffer[SECTOR_SIZE];

int disk_read_sector(int lba, unsigned char* buffer);
int disk_write_sector(int lba, unsigned char* buffer);

int get_root_dir_start(void);
int get_data_start(void);
int get_root_dir_sectors(void);
unsigned int get_total_clusters(void);
int fat_is_data_cluster(unsigned int cluster);
unsigned short fat_end_of_chain_value(void);
int fat16_write_fat_entry(unsigned short cluster, unsigned short value);
unsigned short fat16_get_next_cluster(unsigned short cluster);
int fat16_alloc_cluster_internal(unsigned short* out_cluster);
int fat16_free_cluster_chain_internal(unsigned short start_cluster);
int fat16_zero_cluster(unsigned short cluster);

int fat16_match_name(const char* filename, const FAT16_DirectoryEntry* entry);
int fat16_format_name(const char* input, unsigned char out_name[11]);
int fat16_find_entry_internal(
    unsigned int dir_cluster,
    const char* name,
    FAT16_DirectoryEntry* out_entry,
    int* out_sector,
    int* out_index
);

#endif

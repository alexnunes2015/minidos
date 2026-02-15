#ifndef FAT16_H
#define FAT16_H

#define SECTOR_SIZE 512
#define FAT16_ATTR_READ_ONLY 0x01
#define FAT16_ATTR_HIDDEN    0x02
#define FAT16_ATTR_SYSTEM    0x04
#define FAT16_ATTR_VOLUME_ID 0x08
#define FAT16_ATTR_DIRECTORY 0x10
#define FAT16_ATTR_ARCHIVE   0x20

typedef struct {
    unsigned char  name[11];        // 8.3 filename
    unsigned char  attributes;
    unsigned char  reserved;
    unsigned char  creation_time_ms;
    unsigned short creation_time;
    unsigned short creation_date;
    unsigned short access_date;
    unsigned short cluster_high;    // FAT32 only, 0 for FAT16
    unsigned short modified_time;
    unsigned short modified_date;
    unsigned short cluster_low;     // First cluster
    unsigned int   file_size;
} __attribute__((packed)) FAT16_DirectoryEntry;

typedef struct {
    unsigned short bytes_per_sector;
    unsigned char  sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char  fat_count;
    unsigned short root_entries;
    unsigned short total_sectors_16;
    unsigned char  media_type;
    unsigned short sectors_per_fat;
    unsigned short sectors_per_track;
    unsigned short heads;
    unsigned int   hidden_sectors;
    unsigned int   total_sectors_32;
} __attribute__((packed)) FAT16_BPB;

int fat16_init();
void fat16_set_drive(int drive_letter);
void fat16_list_root();
void fat16_list_dir(unsigned int dir_cluster);
void fat16_list_root_filtered(const char* pattern);
void fat16_list_dir_filtered(unsigned int dir_cluster, const char* pattern);
int fat16_read_file(const char* filename, unsigned char* buffer, int max_size);
int fat16_read_file_from_dir(unsigned int dir_cluster, const char* filename, unsigned char* buffer, int max_size);
int fat16_find_dir_cluster(unsigned int dir_cluster, const char* name, unsigned int* out_cluster);
int fat16_get_parent_cluster(unsigned int dir_cluster, unsigned int* out_cluster);
int fat16_find_entry(unsigned int dir_cluster, const char* name, FAT16_DirectoryEntry* out_entry, int* out_sector, int* out_index);
int fat16_create_entry(unsigned int dir_cluster, const char* name, unsigned short first_cluster, unsigned int file_size, unsigned char attributes);
int fat16_update_entry(unsigned int dir_cluster, const char* old_name, const char* new_name, unsigned short first_cluster, unsigned int file_size, unsigned char attributes);
int fat16_delete_entry(unsigned int dir_cluster, const char* name, int free_cluster_chain);
int fat16_delete_matching(unsigned int dir_cluster, const char* pattern, int free_cluster_chain, int* deleted_count);
int fat16_copy_file(unsigned int dir_cluster, const char* src_name, const char* dst_name);
int fat16_copy_file_between_dirs(unsigned int src_dir_cluster, const char* src_name, unsigned int dst_dir_cluster, const char* dst_name);
int fat16_get_entry_by_index(unsigned int dir_cluster, unsigned int index, char* out_name, int out_name_size, int* out_is_dir, unsigned int* out_file_size);
int fat16_alloc_cluster(unsigned short* out_cluster);
int fat16_free_cluster_chain(unsigned short start_cluster);
int fat16_mkdir(unsigned int dir_cluster, const char* name);
int fat16_rmdir(unsigned int dir_cluster, const char* name);

#endif

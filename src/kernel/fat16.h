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

void fat16_init();
void fat16_set_drive(int drive_letter);
void fat16_list_root();
int fat16_read_file(const char* filename, unsigned char* buffer, int max_size);

#endif

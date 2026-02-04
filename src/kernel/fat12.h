#ifndef FAT12_H
#define FAT12_H

typedef struct {
    unsigned char name[8];
    unsigned char ext[3];
    unsigned char attr;
    unsigned char reserved[10];
    unsigned short time;
    unsigned short date;
    unsigned short first_cluster;
    unsigned int size;
} __attribute__((packed)) FAT_DirectoryEntry;

void fat_list_root();
void fat_type_file(const char* filename);

#endif

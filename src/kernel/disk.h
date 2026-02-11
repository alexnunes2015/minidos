#ifndef DISK_H
#define DISK_H

// Initialize disk driver
void disk_init();

// Read a sector from LBA (primary master disk)
int disk_read_lba(unsigned int lba, unsigned char* buffer);

// Read a sector from specific disk (0=primary master, 1=primary slave, etc)
int disk_read_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer);

// Write a sector to LBA (future)
int disk_write_lba(unsigned int lba, unsigned char* buffer);

// Write a sector to specific disk (0=primary master, 1=primary slave, etc)
int disk_write_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer);

#endif

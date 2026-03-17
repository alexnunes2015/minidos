#ifndef DISK_H
#define DISK_H

// Initialize disk driver
void disk_init();

// Return non-zero when the boot media is a BIOS floppy drive.
int disk_boot_media_is_floppy(void);

// Total sectors reported by the boot media BPB (0 when unknown).
unsigned int disk_boot_media_total_sectors(void);

// Return non-zero when the selected runtime disk is present.
int disk_is_present(unsigned char disk_id);

// Read a sector from LBA (primary master disk)
int disk_read_lba(unsigned int lba, unsigned char* buffer);

// Read a sector from specific disk (0=primary master, 1=primary slave, etc)
int disk_read_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer);

// Write a sector to LBA (primary master disk)
int disk_write_lba(unsigned int lba, unsigned char* buffer);

// Write a sector to specific disk (0=primary master, 1=primary slave, etc)
int disk_write_lba_from_disk(unsigned char disk_id, unsigned int lba, unsigned char* buffer);

#endif

#pragma once
#include "types.h"
#include "stdbool.h"

#define FAT16_EOF 0xFFFF	  /* End Of File (также 0xFFF8..0xFFFF) */
#define FAT16_BAD 0xFFF7	  /* Bad cluster */
#define FAT16_FREE 0x0000	  /* Free cluster */
#define FAT16_RESERVED 0xFFF0 /* Reserved (0xFFF0..0xFFF6) */

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME 0x0F

#pragma pack(push, 1)
typedef struct
{
	uint8_t BS_JmpBoot[3];	 /* 0 */
	uint8_t BS_OEMName[8];	 /* 3 */
	uint16_t BPB_BytsPerSec; /* 11 */
	uint8_t BPB_SecPerClus;	 /* 13 */
	uint16_t BPB_RsvdSecCnt; /* 14 */
	uint8_t BPB_NumFATs;	 /* 16 */
	uint16_t BPB_RootEntCnt; /* 17 */
	uint16_t BPB_TotSec16;	 /* 19 */
	uint8_t BPB_Media;		 /* 21 */
	uint16_t BPB_FATSz16;	 /* 22 */
	uint16_t BPB_SecPerTrk;	 /* 24 */
	uint16_t BPB_NumHeads;	 /* 26 */
	uint32_t BPB_HiddSec;	 /* 28 */
	uint32_t BPB_TotSec32;	 /* 32 */

	uint8_t BS_DrvNum;		  /* 36 */
	uint8_t BS_Reserved1;	  /* 37 */
	uint8_t BS_BootSig;		  /* 38 */
	uint32_t BS_VolID;		  /* 39 */
	uint8_t BS_VolLab[11];	  /* 43 */
	uint8_t BS_FilSysType[8]; /* 54 */

	uint8_t BS_BootCode[448]; /* 62 */
	uint16_t BS_Sign;		  /* 510, must be 0xAA55 */
} FAT16_BootSector;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	uint8_t DIR_Name[11];	  /* 0:  */
	uint8_t DIR_Attr;		  /* 11:  */
	uint8_t DIR_NTRes;		  /* 12:  */
	uint8_t DIR_CrtTimeTenth; /* 13:  */
	uint16_t DIR_CrtTime;	  /* 14:  */
	uint16_t DIR_CrtDate;	  /* 16:  */
	uint16_t DIR_LstAccDate;  /* 18:  */
	uint16_t DIR_FstClusHI;	  /* 20:  */
	uint16_t DIR_WrtTime;	  /* 22:  */
	uint16_t DIR_WrtDate;	  /* 24:  */
	uint16_t DIR_FstClusLO;	  /* 26: */
	uint32_t DIR_FileSize;	  /* 28:  */
} FAT16_DirectoryEntry;
#pragma pack(pop)

typedef struct
{
	FAT16_BootSector bpb;
	uint16_t bytes_per_sector;
	uint8_t sec_per_clus;
	uint16_t rsvd_sec_cnt;
	uint8_t num_fats;
	uint16_t root_ent_cnt;
	uint16_t fat_sz16;
	uint32_t fat_start_lba;
	uint32_t root_dir_lba;
	uint32_t data_start_lba;
	uint32_t root_dir_sectors;
	uint32_t total_sectors;
	uint32_t total_clusters;
} FAT16_FS;

bool fat16_check_boot_sector(const FAT16_BootSector *bs);
void fat16_init(FAT16_BootSector *bs);
int read_boot_sector(FAT16_BootSector *bs);
int fat16_mount(FAT16_FS *fs);
int fat16_format(FAT16_FS *fs, uint32_t total_sectors);
int fat16_mount_or_format(FAT16_FS *fs, uint32_t total_sectors);
int fat16_read_fat_entry(const FAT16_FS *fs, uint16_t cluster, uint16_t *next_cluster);
int fat16_write_fat_entry(const FAT16_FS *fs, uint16_t cluster, uint16_t value);
int fat16_find_root_entry(const FAT16_FS *fs, const char *name, FAT16_DirectoryEntry *entry);
int fat16_read_file(const FAT16_FS *fs, const FAT16_DirectoryEntry *entry, uint8_t *buffer, uint32_t buffer_size, uint32_t *bytes_read);
int fat16_write_file(FAT16_FS *fs, const char *name, const uint8_t *data, uint32_t size);
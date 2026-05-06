#include "fat16.h"
#include "printk.h"
#include "memory.h"
#include "drivers/disk/sd.h"

static void put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
	p[2] = (uint8_t)((v >> 16) & 0xFFu);
	p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
		   ((uint32_t)p[1] << 8) |
		   ((uint32_t)p[2] << 16) |
		   ((uint32_t)p[3] << 24);
}

static uint8_t upper_ascii(uint8_t c)
{
	if (c >= 'a' && c <= 'z')
	{
		return (uint8_t)(c - ('a' - 'A'));
	}
	return c;
}

static int make_name_83(const char *name, uint8_t out[11])
{
	int i = 0;
	int j = 0;
	int ext = 0;

	for (i = 0; i < 11; i++)
	{
		out[i] = ' ';
	}

	i = 0;
	while (name[i] != '\0')
	{
		char ch = name[i++];
		if (ch == '.')
		{
			ext = 1;
			j = 8;
			continue;
		}

		if ((!ext && j >= 8) || (ext && j >= 11))
		{
			return -1;
		}

		out[j++] = upper_ascii((uint8_t)ch);
	}

	return 0;
}

static uint32_t cluster_to_lba(const FAT16_FS *fs, uint16_t cluster)
{
	return fs->data_start_lba + ((uint32_t)(cluster - 2u) * fs->sec_per_clus);
}

static uint32_t calc_root_dir_sectors(uint16_t root_entries, uint16_t bytes_per_sec)
{
	return ((uint32_t)root_entries * 32u + ((uint32_t)bytes_per_sec - 1u)) / (uint32_t)bytes_per_sec;
}

static uint16_t choose_sec_per_clus(uint32_t total_sectors)
{
	/* Conservative defaults for FAT16. */
	if (total_sectors < 1u << 15)
		return 1;
	if (total_sectors < 1u << 16)
		return 2;
	if (total_sectors < 1u << 17)
		return 4;
	if (total_sectors < 1u << 18)
		return 8;
	if (total_sectors < 1u << 19)
		return 16;
	if (total_sectors < 1u << 20)
		return 32;
	return 64;
}

static uint16_t calc_fat_sz16(uint32_t total_sectors, uint16_t bytes_per_sec, uint8_t sec_per_clus, uint16_t reserved, uint8_t num_fats, uint16_t root_entries)
{
	uint32_t root_dir_sectors = calc_root_dir_sectors(root_entries, bytes_per_sec);
	uint32_t fat_sz = 1;

	for (;;)
	{
		uint32_t data_sectors;
		uint32_t clusters;
		uint32_t need;
		uint32_t overhead = (uint32_t)reserved + (uint32_t)num_fats * fat_sz + root_dir_sectors;
		if (total_sectors <= overhead)
		{
			return 1;
		}
		data_sectors = total_sectors - overhead;
		clusters = data_sectors / (uint32_t)sec_per_clus;
		need = ((clusters + 2u) * 2u + ((uint32_t)bytes_per_sec - 1u)) / (uint32_t)bytes_per_sec;
		if (need <= fat_sz)
		{
			break;
		}
		fat_sz = need;
	}

	if (fat_sz > 0xFFFFu)
	{
		return 0;
	}

	return (uint16_t)fat_sz;
}

static void fat16_set_dir_entry(FAT16_DirectoryEntry *e, const uint8_t name83[11], uint16_t first_cluster, uint32_t size)
{
	memset(e, 0, sizeof(*e));
	memcpy(e->DIR_Name, name83, 11);
	e->DIR_Attr = ATTR_ARCHIVE;
	put_le16((uint8_t *)&e->DIR_FstClusLO, first_cluster);
	put_le32((uint8_t *)&e->DIR_FileSize, size);
}

static int fat16_free_chain(const FAT16_FS *fs, uint16_t start_cluster)
{
	uint16_t c = start_cluster;
	while (c >= 2 && c < FAT16_RESERVED)
	{
		uint16_t next;
		if (fat16_read_fat_entry(fs, c, &next) != 0)
			return -1;
		if (fat16_write_fat_entry(fs, c, FAT16_FREE) != 0)
			return -1;
		if (next >= 0xFFF8u)
			break;
		c = next;
	}
	return 0;
}

static int fat16_alloc_chain(FAT16_FS *fs, uint32_t clusters_needed, uint16_t *first_cluster)
{
	uint16_t first = 0;
	uint16_t prev = 0;
	uint32_t got = 0;

	for (uint32_t c = 2; c < fs->total_clusters + 2u && got < clusters_needed; c++)
	{
		uint16_t entry;
		if (fat16_read_fat_entry(fs, (uint16_t)c, &entry) != 0)
			return -1;
		if (entry != FAT16_FREE)
			continue;

		if (first == 0)
		{
			first = (uint16_t)c;
		}
		if (prev != 0)
		{
			if (fat16_write_fat_entry(fs, prev, (uint16_t)c) != 0)
				return -1;
		}
		prev = (uint16_t)c;
		got++;
	}

	if (got != clusters_needed || prev == 0)
	{
		if (first)
			fat16_free_chain(fs, first);
		return -1;
	}

	if (fat16_write_fat_entry(fs, prev, FAT16_EOF) != 0)
	{
		fat16_free_chain(fs, first);
		return -1;
	}

	*first_cluster = first;
	return 0;
}

bool fat16_check_boot_sector(const FAT16_BootSector *bs)
{
	const uint8_t *raw = (const uint8_t *)bs;
	uint16_t bps;
	uint8_t spc;
	uint16_t rsvd;
	uint8_t nfats;
	uint16_t root_cnt;
	uint16_t fatsz;
	uint16_t sign;

	if (!bs)
	{
		return false;
	}

	if (!((raw[0] == 0xEB && raw[2] == 0x90) || raw[0] == 0xE9))
	{
		return false;
	}

	bps = get_le16(&raw[11]);
	if (!(bps == 512 || bps == 1024 || bps == 2048 || bps == 4096))
	{
		return false;
	}

	spc = raw[13];
	if (!(spc == 1 || spc == 2 || spc == 4 || spc == 8 || spc == 16 || spc == 32 || spc == 64 || spc == 128))
	{
		return false;
	}

	rsvd = get_le16(&raw[14]);
	nfats = raw[16];
	root_cnt = get_le16(&raw[17]);
	fatsz = get_le16(&raw[22]);
	sign = get_le16(&raw[510]);

	if (rsvd == 0 || nfats == 0 || root_cnt == 0 || fatsz == 0)
	{
		return false;
	}

	if (sign != 0xAA55)
	{
		return false;
	}

	return true;
}

__attribute__((noinline)) void fat16_init(FAT16_BootSector *bs)
{
	if (!bs)
	{
		return;
	}

	bs->BS_JmpBoot[0] = 0xEB;
	bs->BS_JmpBoot[1] = 0x3C;
	bs->BS_JmpBoot[2] = 0x90;

	bs->BS_OEMName[0] = 'M';
	bs->BS_OEMName[1] = 'S';
	bs->BS_OEMName[2] = 'D';
	bs->BS_OEMName[3] = 'O';
	bs->BS_OEMName[4] = 'S';
	bs->BS_OEMName[5] = '5';
	bs->BS_OEMName[6] = '.';
	bs->BS_OEMName[7] = '0';

	put_le16((uint8_t *)&bs->BPB_BytsPerSec, 512);
	bs->BPB_SecPerClus = 1;
	put_le16((uint8_t *)&bs->BPB_RsvdSecCnt, 1);
	bs->BPB_NumFATs = 2;
	put_le16((uint8_t *)&bs->BPB_RootEntCnt, 512);
	put_le16((uint8_t *)&bs->BPB_TotSec16, 2880);
	bs->BPB_Media = 0xF0;
	put_le16((uint8_t *)&bs->BPB_FATSz16, 9);
	put_le16((uint8_t *)&bs->BPB_SecPerTrk, 18);
	put_le16((uint8_t *)&bs->BPB_NumHeads, 2);
	put_le32((uint8_t *)&bs->BPB_HiddSec, 0);
	put_le32((uint8_t *)&bs->BPB_TotSec32, 0);

	bs->BS_DrvNum = 0x00;
	bs->BS_Reserved1 = 0x00;
	bs->BS_BootSig = 0x29;
	put_le32((uint8_t *)&bs->BS_VolID, 0x12345678u);

	for (int i = 0; i < 11; i++)
	{
		bs->BS_VolLab[i] = "NO NAME    "[i];
	}
	for (int i = 0; i < 8; i++)
	{
		bs->BS_FilSysType[i] = "FAT16   "[i];
	}

	memset(bs->BS_BootCode, 0, 448);

	put_le16((uint8_t *)&bs->BS_Sign, 0xAA55);
}

int read_boot_sector(FAT16_BootSector *bs)
{
	int res = sd_readblock(0, (uint8_t *)bs, 1);
	if (res <= 0)
	{
		return -1;
	}

	if (!fat16_check_boot_sector(bs))
	{
		return -1;
	}

	return 1;
}

int fat16_mount(FAT16_FS *fs)
{
	const uint8_t *raw;
	uint32_t fatsz;
	uint32_t totsec;
	uint32_t data_sectors;
	uint32_t overhead;
	uint16_t bps;
	uint16_t rsvd;
	uint16_t root_cnt;
	uint16_t tot16;
	uint16_t fatsz16;
	uint32_t tot32;
	uint8_t spc;
	uint8_t nfats;

	if (!fs)
	{
		return -1;
	}

	if (read_boot_sector(&fs->bpb) <= 0)
	{
		return -1;
	}

	raw = (const uint8_t *)&fs->bpb;
	bps = get_le16(&raw[11]);
	spc = raw[13];
	rsvd = get_le16(&raw[14]);
	nfats = raw[16];
	root_cnt = get_le16(&raw[17]);
	tot16 = get_le16(&raw[19]);
	fatsz16 = get_le16(&raw[22]);
	tot32 = get_le32(&raw[32]);

	if (bps != 512)
	{
		printk("FAT16: unsupported sector size %u\n", (unsigned)bps);
		return -1;
	}
	if (spc == 0 || nfats == 0 || fatsz16 == 0)
	{
		return -1;
	}

	fs->bytes_per_sector = bps;
	fs->sec_per_clus = spc;
	fs->rsvd_sec_cnt = rsvd;
	fs->num_fats = nfats;
	fs->root_ent_cnt = root_cnt;
	fs->fat_sz16 = fatsz16;

	fs->root_dir_sectors = ((uint32_t)fs->root_ent_cnt * 32u + (fs->bytes_per_sector - 1u)) / fs->bytes_per_sector;
	fs->fat_start_lba = fs->rsvd_sec_cnt;
	fatsz = fs->fat_sz16;
	fs->root_dir_lba = fs->fat_start_lba + (uint32_t)fs->num_fats * fatsz;
	fs->data_start_lba = fs->root_dir_lba + fs->root_dir_sectors;
	totsec = tot16 ? tot16 : tot32;
	fs->total_sectors = totsec;

	overhead = fs->rsvd_sec_cnt + (uint32_t)fs->num_fats * fatsz + fs->root_dir_sectors;
	if (totsec <= overhead)
	{
		return -1;
	}
	data_sectors = totsec - overhead;
	fs->total_clusters = data_sectors / fs->sec_per_clus;
	if (fs->total_clusters == 0)
	{
		return -1;
	}

	return 0;
}

int fat16_format(FAT16_FS *fs, uint32_t total_sectors)
{
	FAT16_BootSector bs;
	uint8_t sec[512];
	uint16_t sec_per_clus;
	uint16_t fat_sz;
	uint32_t root_dir_sectors;

	if (!fs || total_sectors < 100u)
	{
		return -1;
	}

	fat16_init(&bs);

	sec_per_clus = choose_sec_per_clus(total_sectors);
	fat_sz = calc_fat_sz16(total_sectors, 512, (uint8_t)sec_per_clus, 1, 2, 512);
	if (fat_sz == 0)
	{
		return -1;
	}

	bs.BPB_SecPerClus = (uint8_t)sec_per_clus;
	bs.BPB_Media = 0xF8;
	put_le16((uint8_t *)&bs.BPB_FATSz16, fat_sz);
	if (total_sectors <= 0xFFFFu)
	{
		put_le16((uint8_t *)&bs.BPB_TotSec16, (uint16_t)total_sectors);
		put_le32((uint8_t *)&bs.BPB_TotSec32, 0);
	}
	else
	{
		put_le16((uint8_t *)&bs.BPB_TotSec16, 0);
		put_le32((uint8_t *)&bs.BPB_TotSec32, total_sectors);
	}

	if (sd_writeblock(0, (const uint8_t *)&bs, 1) <= 0)
	{
		return -1;
	}

	memset(sec, 0, sizeof(sec));
	sec[0] = bs.BPB_Media;
	sec[1] = 0xFF;
	sec[2] = 0xFF;
	sec[3] = 0xFF;

	for (uint32_t f = 0; f < bs.BPB_NumFATs; f++)
	{
		uint32_t fat_lba = bs.BPB_RsvdSecCnt + f * (uint32_t)fat_sz;
		for (uint32_t s = 0; s < fat_sz; s++)
		{
			if (s != 0)
			{
				memset(sec, 0, sizeof(sec));
			}
			if (sd_writeblock(fat_lba + s, sec, 1) <= 0)
			{
				return -1;
			}
		}
	}

	memset(sec, 0, sizeof(sec));
	root_dir_sectors = calc_root_dir_sectors(bs.BPB_RootEntCnt, 512);
	for (uint32_t s = 0; s < root_dir_sectors; s++)
	{
		uint32_t lba = bs.BPB_RsvdSecCnt + (uint32_t)bs.BPB_NumFATs * (uint32_t)fat_sz + s;
		if (sd_writeblock(lba, sec, 1) <= 0)
		{
			return -1;
		}
	}

	return fat16_mount(fs);
}

int fat16_mount_or_format(FAT16_FS *fs, uint32_t total_sectors)
{
	if (fat16_mount(fs) == 0)
	{
		return 0;
	}
	return fat16_format(fs, total_sectors);
}

int fat16_read_fat_entry(const FAT16_FS *fs, uint16_t cluster, uint16_t *next_cluster)
{
	uint8_t sector[512];
	uint32_t fat_offset;
	uint32_t fat_lba;
	uint32_t offset;

	if (!fs || !next_cluster || cluster < 2)
	{
		return -1;
	}

	fat_offset = (uint32_t)cluster * 2u;
	fat_lba = fs->fat_start_lba + (fat_offset / 512u);
	offset = fat_offset & 0x1FFu;

	if (sd_readblock(fat_lba, sector, 1) <= 0)
	{
		return -1;
	}

	if (offset == 511u)
	{
		uint8_t next_sector[512];
		if (sd_readblock(fat_lba + 1u, next_sector, 1) <= 0)
		{
			return -1;
		}
		*next_cluster = (uint16_t)sector[511] | ((uint16_t)next_sector[0] << 8);
	}
	else
	{
		*next_cluster = (uint16_t)sector[offset] | ((uint16_t)sector[offset + 1u] << 8);
	}

	return 0;
}

int fat16_write_fat_entry(const FAT16_FS *fs, uint16_t cluster, uint16_t value)
{
	uint32_t fat_offset;
	uint32_t offset;
	uint8_t sector[512];

	if (!fs || cluster < 2)
	{
		return -1;
	}

	fat_offset = (uint32_t)cluster * 2u;
	offset = fat_offset & 0x1FFu;

	for (uint32_t f = 0; f < fs->num_fats; f++)
	{
		uint32_t fat_base = fs->fat_start_lba + f * (uint32_t)fs->fat_sz16;
		uint32_t fat_lba = fat_base + (fat_offset / 512u);
		if (sd_readblock(fat_lba, sector, 1) <= 0)
			return -1;

		if (offset == 511u)
		{
			uint8_t next_sector[512];
			if (sd_readblock(fat_lba + 1u, next_sector, 1) <= 0)
				return -1;
			sector[511] = (uint8_t)(value & 0xFFu);
			next_sector[0] = (uint8_t)((value >> 8) & 0xFFu);
			if (sd_writeblock(fat_lba, sector, 1) <= 0)
				return -1;
			if (sd_writeblock(fat_lba + 1u, next_sector, 1) <= 0)
				return -1;
		}
		else
		{
			sector[offset] = (uint8_t)(value & 0xFFu);
			sector[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
			if (sd_writeblock(fat_lba, sector, 1) <= 0)
				return -1;
		}
	}

	return 0;
}

int fat16_find_root_entry(const FAT16_FS *fs, const char *name, FAT16_DirectoryEntry *entry)
{
	uint8_t target[11];
	uint8_t sector[512];
	uint32_t s;

	if (!fs || !name || !entry)
	{
		return -1;
	}

	if (make_name_83(name, target) != 0)
	{
		return -1;
	}

	for (s = 0; s < fs->root_dir_sectors; s++)
	{
		if (sd_readblock(fs->root_dir_lba + s, sector, 1) <= 0)
		{
			return -1;
		}

		for (uint32_t off = 0; off < 512; off += 32)
		{
			FAT16_DirectoryEntry *e = (FAT16_DirectoryEntry *)&sector[off];
			if (e->DIR_Name[0] == 0x00)
			{
				return -1;
			}
			if (e->DIR_Name[0] == 0xE5)
			{
				continue;
			}
			if ((e->DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME)
			{
				continue;
			}

			int match = 1;
			for (int i = 0; i < 11; i++)
			{
				if (upper_ascii(e->DIR_Name[i]) != target[i])
				{
					match = 0;
					break;
				}
			}

			if (match)
			{
				memcpy(entry, e, sizeof(FAT16_DirectoryEntry));
				return 0;
			}
		}
	}

	return -1;
}

int fat16_read_file(const FAT16_FS *fs, const FAT16_DirectoryEntry *entry, uint8_t *buffer, uint32_t buffer_size, uint32_t *bytes_read)
{
	uint16_t cluster;
	uint32_t remaining;
	uint32_t written = 0;
	uint8_t sector[512];

	if (!fs || !entry || !buffer)
	{
		return -1;
	}

	cluster = get_le16((const uint8_t *)&entry->DIR_FstClusLO);
	remaining = get_le32((const uint8_t *)&entry->DIR_FileSize);

	if (remaining > buffer_size)
	{
		return -1;
	}

	while (remaining > 0 && cluster >= 2 && cluster < FAT16_RESERVED)
	{
		uint32_t lba = cluster_to_lba(fs, cluster);
		for (uint8_t s = 0; s < fs->sec_per_clus && remaining > 0; s++)
		{
			if (sd_readblock(lba + s, sector, 1) <= 0)
			{
				return -1;
			}

			uint32_t chunk = (remaining > 512u) ? 512u : remaining;
			memcpy(&buffer[written], sector, chunk);
			written += chunk;
			remaining -= chunk;
		}

		if (remaining == 0)
		{
			break;
		}

		uint16_t next;
		if (fat16_read_fat_entry(fs, cluster, &next) != 0)
		{
			return -1;
		}
		cluster = next;
	}

	if (remaining != 0)
	{
		return -1;
	}

	if (bytes_read)
	{
		*bytes_read = written;
	}

	return 0;
}

int fat16_write_file(FAT16_FS *fs, const char *name, const uint8_t *data, uint32_t size)
{
	uint8_t name83[11];
	uint32_t needed_clusters;
	uint16_t first_cluster = 0;
	uint16_t current;
	uint32_t remaining;
	uint32_t cursor = 0;
	uint8_t sec[512];
	uint32_t found_lba = 0;
	uint32_t found_off = 0xFFFFFFFFu;
	uint32_t free_lba = 0;
	uint32_t free_off = 0xFFFFFFFFu;
	int has_existing = 0;
	FAT16_DirectoryEntry existing;

	if (!fs || !name || (!data && size > 0))
	{
		return -1;
	}

	if (make_name_83(name, name83) != 0)
	{
		return -1;
	}

	for (uint32_t s = 0; s < fs->root_dir_sectors; s++)
	{
		uint32_t lba = fs->root_dir_lba + s;
		if (sd_readblock(lba, sec, 1) <= 0)
			return -1;

		for (uint32_t off = 0; off < 512; off += 32)
		{
			FAT16_DirectoryEntry *e = (FAT16_DirectoryEntry *)&sec[off];
			if (e->DIR_Name[0] == 0x00)
			{
				if (free_off == 0xFFFFFFFFu)
				{
					free_lba = lba;
					free_off = off;
				}
				goto root_scan_done;
			}
			if (e->DIR_Name[0] == 0xE5)
			{
				if (free_off == 0xFFFFFFFFu)
				{
					free_lba = lba;
					free_off = off;
				}
				continue;
			}
			if ((e->DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME)
				continue;

			int match = 1;
			for (int i = 0; i < 11; i++)
			{
				if (upper_ascii(e->DIR_Name[i]) != name83[i])
				{
					match = 0;
					break;
				}
			}
			if (match)
			{
				has_existing = 1;
				memcpy(&existing, e, sizeof(existing));
				found_lba = lba;
				found_off = off;
				goto root_scan_done;
			}
		}
	}

root_scan_done:
	if (!has_existing && free_off == 0xFFFFFFFFu)
	{
		return -1;
	}

	if (has_existing)
	{
		uint16_t old_first = get_le16((const uint8_t *)&existing.DIR_FstClusLO);
		if (old_first >= 2)
		{
			if (fat16_free_chain(fs, old_first) != 0)
				return -1;
		}
	}

	if (size > 0)
	{
		uint32_t cluster_bytes = (uint32_t)fs->sec_per_clus * 512u;
		needed_clusters = (size + cluster_bytes - 1u) / cluster_bytes;
		if (fat16_alloc_chain(fs, needed_clusters, &first_cluster) != 0)
			return -1;

		current = first_cluster;
		remaining = size;
		while (remaining > 0)
		{
			uint32_t lba = cluster_to_lba(fs, current);
			for (uint8_t s = 0; s < fs->sec_per_clus; s++)
			{
				uint32_t chunk;
				if (remaining == 0)
				{
					memset(sec, 0, sizeof(sec));
				}
				else
				{
					chunk = (remaining > 512u) ? 512u : remaining;
					memset(sec, 0, sizeof(sec));
					memcpy(sec, &data[cursor], chunk);
					cursor += chunk;
					remaining -= chunk;
				}
				if (sd_writeblock(lba + s, sec, 1) <= 0)
					return -1;
			}

			if (remaining == 0)
				break;

			if (fat16_read_fat_entry(fs, current, &current) != 0)
				return -1;
			if (current >= 0xFFF8u)
				return -1;
		}
	}

	{
		uint32_t lba = has_existing ? found_lba : free_lba;
		uint32_t off = has_existing ? found_off : free_off;
		if (sd_readblock(lba, sec, 1) <= 0)
			return -1;
		fat16_set_dir_entry((FAT16_DirectoryEntry *)&sec[off], name83, first_cluster, size);
		if (sd_writeblock(lba, sec, 1) <= 0)
			return -1;
	}

	return 0;
}
#include "strconv.h"
#include "drivers/disk/sd.h"
#include "types.h"
#include "printk.h"
#include "fs/fat16.h"
// #include "dtb.h"

#define MT_NORMAL 1
#define MT_DEVICE 0

#define DESC_VALID (1ULL << 0)
#define DESC_BLOCK (0ULL << 1)
#define DESC_TABLE (1ULL << 1)
#define DESC_AF (1ULL << 10)
#define DESC_SH_INNER (3ULL << 8)
#define DESC_AP_RW_EL1 (0ULL << 6)
#define DESC_ATTR(idx) (((unsigned long long)(idx)) << 2)

unsigned long long int table_l1[512] __attribute__((used, section(".data"), aligned(4096))) = {0};

void mmu_build_tables(void)
{
	for (int i = 0; i < 512; i++)
	{
		unsigned long long pa = (unsigned long long)i << 30;
		table_l1[i] = pa | DESC_VALID | DESC_BLOCK | DESC_AF | DESC_SH_INNER | DESC_AP_RW_EL1 | DESC_ATTR(MT_NORMAL);
	}
}

unsigned long long dtb_addr;

extern void enable_mmu(unsigned long long int *table_base);

void kmain(void)
{
	printk("Hello, kernel!\n");

	// mmu_build_tables();

	// // Включаем MMU
	// enable_mmu(table_l1);

	int init_rc = sd_init();
	if (init_rc != SD_OK)
	{
		printk("SD init: ERROR rc=%d\n", init_rc);
		while (1)
		{
		}
	}
	printk("SD init: OK\n");

	FAT16_FS fs;
	const uint32_t fallback_total_sectors = 131072; /* 64 MiB image with 512-byte sectors. */
	int mrc = fat16_mount_or_format(&fs, fallback_total_sectors);
	if (mrc != 0)
	{
		printk("FAT16 mount/format: ERROR rc=%d\n", mrc);
		while (1)
		{
		}
	}
	printk("FAT16 mount/format: OK\n");

	static const uint8_t msg[] = "hello from kernel\n";
	int wrc = fat16_write_file(&fs, "HELLO.TXT", msg, (uint32_t)(sizeof(msg) - 1u));
	if (wrc != 0)
	{
		printk("FAT16 write file: ERROR rc=%d\n", wrc);
		while (1)
		{
		}
	}
	printk("FAT16 write file: OK\n");

	FAT16_DirectoryEntry entry;
	if (fat16_find_root_entry(&fs, "HELLO.TXT", &entry) != 0)
	{
		printk("FAT16 find file: ERROR\n");
	}
	else
	{
		uint8_t verify[64];
		uint32_t rd = 0;
		if (fat16_read_file(&fs, &entry, verify, sizeof(verify) - 1u, &rd) == 0)
		{
			verify[rd] = 0;
			printk("FAT16 readback (%u bytes): %s", (unsigned int)rd, (const char *)verify);
		}
		else
		{
			printk("FAT16 readback: ERROR\n");
		}
	}

	// Теперь MMU включен, но мы все еще используем физические адреса для UART!
	// Нужно переключиться на виртуальные адреса

	while (1)
	{
		// Бесконечный цикл
	}
}
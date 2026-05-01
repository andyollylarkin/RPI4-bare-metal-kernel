#include "strconv.h"
#include "drivers/disk/sd.h"
#include "types.h"
#include "printk.h"
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

	uint8_t buffer[512];
	for (int i = 0; i < 512; i++)
	{
		buffer[i] = i & 0xFF;
	}

	int init_rc = sd_init();
	if (init_rc != SD_OK)
	{
		printk("SD init: ERROR rc=%d\n", init_rc);
		while (1)
		{
		}
	}
	printk("SD init: OK\n");

	int res = sd_writeblock(0, buffer, 1);
	if (res > 0)
	{
		printk("SD write block 0: OK, bytes=%d\n", res);
	}
	else
	{
		printk("SD write block 0: ERROR rc=%d sd_err=%d\n", res, sd_get_last_error());
	}

	// Теперь MMU включен, но мы все еще используем физические адреса для UART!
	// Нужно переключиться на виртуальные адреса

	while (1)
	{
		// Бесконечный цикл
	}
}
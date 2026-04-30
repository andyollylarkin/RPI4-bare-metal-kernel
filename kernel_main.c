#include "strconv.h"
#include "drivers/disk/sd.h"
#include "types.h"
// #include "dtb.h"

#define UART_BASE 0xFE201000
#define UART_DR ((volatile unsigned int *)(UART_BASE + 0x00))
#define UART_FR ((volatile unsigned int *)(UART_BASE + 0x18))
#define UART_IBRD ((volatile unsigned int *)(UART_BASE + 0x24))
#define UART_FBRD ((volatile unsigned int *)(UART_BASE + 0x28))
#define UART_LCR_H ((volatile unsigned int *)(UART_BASE + 0x2C))
#define UART_CR ((volatile unsigned int *)(UART_BASE + 0x30))

static void uart_init(void)
{
	*UART_CR = 0;
	*UART_IBRD = 26;
	*UART_FBRD = 0;
	*UART_LCR_H = (1 << 3) | (1 << 4);
	*UART_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

static void uart_putc(char c)
{
	while (*UART_FR & (1 << 5))
	{
	}
	*UART_DR = (unsigned int)c;
}

static void uart_puts(const char *s)
{
	while (*s)
	{
		if (*s == '\n')
			uart_putc('\r');
		uart_putc(*s++);
	}
}

static void uart_putint(int v)
{
	char tmp[16];
	if (v < 0)
	{
		uart_putc('-');
		v = -v;
	}
	itoa(v, tmp, 10);
	uart_puts(tmp);
}

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
	uart_init();
	uart_puts("kmain: start\n");

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
		uart_puts("SD init: ERROR rc=");
		uart_putint(init_rc);
		uart_putc('\n');
		while (1)
		{
		}
	}
	uart_puts("SD init: OK\n");

	// int res = sd_readblock(0, buffer, 1);
	// if (res > 0)
	// {
	// 	uart_puts("SD read block 0: OK\n");
	// 	uart_puts("Data: ");
	// 	for (int i = 0; i < 16; i++)
	// 	{
	// 		char tmp[4];
	// 		itoa(buffer[i], tmp, 16);
	// 		uart_puts(tmp);
	// 		uart_putc(' ');
	// 	}
	// 	uart_putc('\n');
	// }
	// else
	// {
	// 	uart_puts("SD read block 0: ERROR\n");
	// 	uart_puts("rc=");
	// 	uart_putint(res);
	// 	uart_puts(" sd_err=");
	// 	uart_putint(sd_get_last_error());
	// 	uart_putc('\n');
	// }

	int res = sd_writeblock(0, buffer, 1);
	if (res > 0)
	{
		uart_puts("SD write block 100: OK, bytes=");
		uart_putint(res);
		uart_putc('\n');
	}
	else
	{
		uart_puts("SD write block 100: ERROR\n");
		uart_puts("rc=");
		uart_putint(res);
		uart_puts(" sd_err=");
		uart_putint(sd_get_last_error());
		uart_putc('\n');
	}

	// Теперь MMU включен, но мы все еще используем физические адреса для UART!
	// Нужно переключиться на виртуальные адреса

	while (1)
	{
		// Бесконечный цикл
	}
}
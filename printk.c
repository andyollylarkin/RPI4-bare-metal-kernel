#include "uart.h"
#include <stdarg.h>

static void printk_putc(char c)
{
	if (c == '\n')
		uart_send('\r');
	uart_send((unsigned int)c);
}

static void printk_puts(const char *s)
{
	if (!s)
		s = "(null)";
	while (*s)
		printk_putc(*s++);
}

static void printk_putu(unsigned long long v, unsigned base, int upper)
{
	char buf[32];
	int i = 0;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	if (base < 2 || base > 16)
		return;

	if (v == 0)
	{
		printk_putc('0');
		return;
	}

	while (v > 0 && i < (int)sizeof(buf))
	{
		buf[i++] = digits[v % base];
		v /= base;
	}

	while (i > 0)
		printk_putc(buf[--i]);
}

static void printk_puti(long long v)
{
	if (v < 0)
	{
		printk_putc('-');
		printk_putu((unsigned long long)(-(v + 1)) + 1ULL, 10, 0);
	}
	else
	{
		printk_putu((unsigned long long)v, 10, 0);
	}
}

void printk(const char *fmt, ...)
{
	if (!is_uart_init())
	{
		uart_init();
	}

	va_list ap;
	va_start(ap, fmt);

	for (int i = 0; fmt[i] != '\0'; i++)
	{
		if (fmt[i] != '%')
		{
			printk_putc(fmt[i]);
			continue;
		}

		i++;
		if (fmt[i] == '\0')
			break;

		int long_count = 0;
		while (fmt[i] == 'l')
		{
			long_count++;
			i++;
		}

		switch (fmt[i])
		{
		case '%':
			printk_putc('%');
			break;
		case 'c':
			printk_putc((char)va_arg(ap, int));
			break;
		case 's':
			printk_puts(va_arg(ap, const char *));
			break;
		case 'd':
		case 'i':
			if (long_count >= 2)
				printk_puti(va_arg(ap, long long));
			else if (long_count == 1)
				printk_puti((long long)va_arg(ap, long));
			else
				printk_puti((long long)va_arg(ap, int));
			break;
		case 'u':
			if (long_count >= 2)
				printk_putu(va_arg(ap, unsigned long long), 10, 0);
			else if (long_count == 1)
				printk_putu((unsigned long long)va_arg(ap, unsigned long), 10, 0);
			else
				printk_putu((unsigned long long)va_arg(ap, unsigned int), 10, 0);
			break;
		case 'x':
			if (long_count >= 2)
				printk_putu(va_arg(ap, unsigned long long), 16, 0);
			else if (long_count == 1)
				printk_putu((unsigned long long)va_arg(ap, unsigned long), 16, 0);
			else
				printk_putu((unsigned long long)va_arg(ap, unsigned int), 16, 0);
			break;
		case 'X':
			if (long_count >= 2)
				printk_putu(va_arg(ap, unsigned long long), 16, 1);
			else if (long_count == 1)
				printk_putu((unsigned long long)va_arg(ap, unsigned long), 16, 1);
			else
				printk_putu((unsigned long long)va_arg(ap, unsigned int), 16, 1);
			break;
		case 'p':
		{
			unsigned long long p = (unsigned long long)va_arg(ap, void *);
			printk_puts("0x");
			printk_putu(p, 16, 0);
			break;
		}
		default:
			printk_putc('%');
			while (long_count-- > 0)
				printk_putc('l');
			printk_putc(fmt[i]);
			break;
		}
	}

	va_end(ap);
}

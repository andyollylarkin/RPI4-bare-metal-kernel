TARGET = aarch64-none-elf

ASM_SOURCES = $(shell find . -name '*.s')
C_SOURCES = $(shell find . -name '*.c')
OBJECTS = $(ASM_SOURCES:.s=.o) $(C_SOURCES:.c=.o)

CFLAGS = \
    --target=$(TARGET) \
    -march=armv8-a \
    -mcpu=cortex-a72 \
    -ffreestanding \
    -nostdlib \
    -fno-builtin \
    -fno-stack-protector \
    -fno-exceptions \
    -fno-rtti \
    -Wall \
    -Wextra \
    -O0 \
    -g \
	-I. \
	-L. \
	-ldtblib
	

AFLAGS = \
    --target=$(TARGET) \
    -march=armv8-a \
    -ffreestanding \
    -D__ASSEMBLY__

LDFLAGS = \
    --target=$(TARGET) \
    -ffreestanding \
    -nostdlib \
    -static \
    -Wl,-T,linker.ld \
    -Wl,-Map=kernel.map \
    -fuse-ld=lld

all: kernel.elf



%.o: %.s
	clang $(AFLAGS) -c $< -o $@

%.o: %.c
	clang $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJECTS)
	clang $(LDFLAGS) $(OBJECTS) -o $@

kernel.img: kernel.elf
	llvm-objcopy-14 -O binary kernel.elf kernel.img


.PHONY: all run clean disasm run-out run-monitor run-debug

include ./Makefile.run.mk
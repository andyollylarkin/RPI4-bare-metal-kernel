ENTRY(_start)

SECTIONS
{
    /* Kernel starts from here */
    . = 0x80000;
    _kernel_phys_start = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    
    .rodata : {
        *(.rodata .rodata.*)
    }
    
    .data : {
        *(.data .data.*)
    }
    
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
	__bss_end = ALIGN(8);
    
    /* END OF KERNEL MEMORY SPACE */
    . = ALIGN(4096);
    _kernel_phys_end = .;


    /* Stack */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 16384;  /* 16KB стек */
    __stack_top = .;
}
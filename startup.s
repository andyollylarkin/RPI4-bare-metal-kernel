// startup.s - ARMv8 (AArch64) точка входа
.section .text.boot
.global _start

_start:
    // 1. Отключить прерывания
    msr daifset, #0xf

    // Разрешаем запуск только на CPU0, остальные ядра паркуем
    mrs x0, mpidr_el1
    and x0, x0, #0x3
    cbz x0, 1f
park_secondary:
    wfe
    b park_secondary
1:
    
    // 2. Настроить стек (временный, до включения MMU)
    ldr x1, =__stack_top
    mov sp, x1
    
    // 3. Очистить BSS секцию
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    sub x1, x1, x0
    bl  memset_zero
    
    // 4. Включить FPU/SIMD (если нужно)
    // mrs x0, cpacr_el1
    // orr x0, x0, #(3 << 20)   // Enable FPU/SIMD
    // msr cpacr_el1, x0

    bl  kmain

hang:
    wfe
    b hang

// Очистка памяти
memset_zero:
    cbz x1, 2f
1:
    str xzr, [x0], #8
    subs x1, x1, #8
    b.gt 1b
2:
    ret

.global enable_mmu

enable_mmu:
    stp x30, x1, [sp, #-16]!   // сохраняем LR и x1

    // Таблица страниц
    msr ttbr0_el3, x0
    
    // Настройка TCR_EL3 (39-битные VA, 4KB гранула)
    mov x1, #0x19
    msr tcr_el3, x1
    
    // Настройка MAIR_EL3 (attr0 = Normal WBWA)
    mov x1, #0xFF
    msr mair_el3, x1
    
    // Инвалидация TLB для EL3
    tlbi alle3
    dsb sy
    isb
    
    // Включение MMU в SCTLR_EL3
    mrs x1, sctlr_el3
    orr x1, x1, #1      // бит M (MMU enable)
    msr sctlr_el3, x1
    isb
    
    ldp x30, x1, [sp], #16     // восстанавливаем LR и x1
    ret
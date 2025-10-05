.section .text
.global gdt_load
.type gdt_load, @function

gdt_load:
    lgdt (%rdi)

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq

1:
    movw $0x28, %ax
    ltr %ax
    ret

.size gdt_load, . - gdt_load
.section .note.GNU-stack,"",@progbits

.section .text
.global _start
.type _start, @function
.extern kernel_main

_start:
    /* Limine enters in 64-bit mode with a valid stack. Preserve SysV's
       16-byte alignment at the C call boundary. */
    andq $-16, %rsp
    xorq %rbp, %rbp
    call kernel_main

1:
    cli
    hlt
    jmp 1b

.size _start, . - _start

.section .note.GNU-stack,"",@progbits

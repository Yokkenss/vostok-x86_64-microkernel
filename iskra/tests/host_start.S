.section .text
.global _start
.type _start, @function
.extern _I4main

_start:
    xorq %rbp, %rbp
    call _I4main
    movq %rax, %rdi
    movq $60, %rax
    syscall

.size _start, . - _start
.section .note.GNU-stack,"",@progbits

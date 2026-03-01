.section .text.start
.global _start
.type _start, @function
.extern _I4main

_start:
    xorq %rbp, %rbp
    movq %rdi, vostok_bootstrap_capability(%rip)
    movq %rsi, vostok_secondary_capability(%rip)
    call _I4main
    movq %rax, %rdi
    movq $0, %rax
    int $0x80
1:
    jmp 1b

.size _start, . - _start

.global vostok_ipc_send
.type vostok_ipc_send, @function
vostok_ipc_send:
    movq %rcx, %r10
    movq $1, %rax
    int $0x80
    ret

.global vostok_ipc_receive
.type vostok_ipc_receive, @function
vostok_ipc_receive:
    pushq %rsi
    movq $2, %rax
    int $0x80
    popq %rcx
    testq %rax, %rax
    js 1f
    movq %rax, 0(%rcx)
    movq %rdx, 8(%rcx)
    xorq %rax, %rax
1:
    ret

.global vostok_cap_close
.type vostok_cap_close, @function
vostok_cap_close:
    movq $3, %rax
    int $0x80
    ret

.global vostok_console_putc
.type vostok_console_putc, @function
vostok_console_putc:
    movq %rdi, %rsi
    addq $256, %rsi
    movq vostok_bootstrap_capability(%rip), %rdi
    xorq %rdx, %rdx
    xorq %r10, %r10
    movq $1, %rax
    int $0x80
    testq %rax, %rax
    js 1f
    movq vostok_bootstrap_capability(%rip), %rdi
    movq $2, %rax
    int $0x80
1:
    ret

.global vostok_console_getc
.type vostok_console_getc, @function
vostok_console_getc:
    movq vostok_bootstrap_capability(%rip), %rdi
    movq $1, %rsi
    xorq %rdx, %rdx
    xorq %r10, %r10
    movq $1, %rax
    int $0x80
    movq vostok_bootstrap_capability(%rip), %rdi
    movq $2, %rax
    int $0x80
    ret

.global vostok_console_stop
.type vostok_console_stop, @function
vostok_console_stop:
    movq vostok_bootstrap_capability(%rip), %rdi
    movq $2, %rsi
    xorq %rdx, %rdx
    xorq %r10, %r10
    movq $1, %rax
    int $0x80
    ret

.global vostok_uart_putc
.type vostok_uart_putc, @function
vostok_uart_putc:
    movq $4, %rax
    int $0x80
    ret

.global vostok_uart_getc
.type vostok_uart_getc, @function
vostok_uart_getc:
    movq $5, %rax
    int $0x80
    ret

.global vostok_process_dump
.type vostok_process_dump, @function
vostok_process_dump:
    movq vostok_secondary_capability(%rip), %rdi
    movq $6, %rax
    int $0x80
    ret

.global vostok_metrics_dump
.type vostok_metrics_dump, @function
vostok_metrics_dump:
    movq vostok_secondary_capability(%rip), %rdi
    movq $7, %rax
    int $0x80
    ret

.global vostok_demo_ready
.type vostok_demo_ready, @function
vostok_demo_ready:
    movq vostok_secondary_capability(%rip), %rdi
    movq $8, %rax
    int $0x80
    ret

.section .bss
.align 8
vostok_bootstrap_capability:
    .quad 0
vostok_secondary_capability:
    .quad 0

.section .note.GNU-stack,"",@progbits

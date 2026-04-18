.section .text
.global serial_interrupt_entry
.type serial_interrupt_entry, @function
.extern serial_dispatch
.extern process_restore

serial_interrupt_entry:
    cld
    pushq $0
    pushq $33
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call serial_dispatch
    movq %rax, %rsp
    jmp process_restore

.size serial_interrupt_entry, . - serial_interrupt_entry
.section .note.GNU-stack,"",@progbits

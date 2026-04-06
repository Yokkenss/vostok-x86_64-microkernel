.section .text
.global timer_interrupt_entry
.type timer_interrupt_entry, @function
.extern timer_dispatch
.extern process_restore

timer_interrupt_entry:
    cld
    pushq $0
    pushq $32
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
    call timer_dispatch
    movq %rax, %rsp
    jmp process_restore

.size timer_interrupt_entry, . - timer_interrupt_entry
.section .note.GNU-stack,"",@progbits

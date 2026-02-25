.section .text
.global syscall_entry
.type syscall_entry, @function
.extern syscall_dispatch

syscall_entry:
    cld
    pushq $0
    pushq $0x80
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
    call syscall_dispatch
    movq %rax, %rsp
    jmp process_restore

.global process_enter
.type process_enter, @function
process_enter:
    movq %rdi, %rsp

.global process_restore
.type process_restore, @function
process_restore:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.size syscall_entry, . - syscall_entry
.section .note.GNU-stack,"",@progbits

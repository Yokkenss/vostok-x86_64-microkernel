OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    limine_requests PT_LOAD FLAGS(4); /* read */
    text   PT_LOAD FLAGS(5); /* read + execute */
    rodata PT_LOAD FLAGS(4); /* read */
    data   PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
    /* Limine maps 64-bit kernels in the higher half. */
    . = 0xffffffff80000000;

    .limine_requests :
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    .text : ALIGN(4096)
    {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    } :data

    .bss : ALIGN(4096)
    {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note .note.*)
    }
}

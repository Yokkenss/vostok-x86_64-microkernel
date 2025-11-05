# Memory management foundation

Vostok currently manages only memory marked `USABLE` by Limine. Kernel,
bootloader, ACPI, framebuffer, bad, and reserved ranges remain unavailable even
if firmware places them inside the physical-address span.

## Physical allocator

Physical memory is divided into 4096-byte pages. Two bitmaps describe every
page below the highest usable address:

- `managed`: the page belongs to the allocator;
- `allocated`: the page is currently owned by a caller.

All pages begin unavailable. Fully aligned pages from `USABLE` memory-map ranges
then become managed and free. The bitmap's own physical pages are finally
removed from the managed set, so an accidental free of allocator metadata is
rejected like any other reserved-page free.

This representation intentionally distinguishes three states:

```text
reserved              managed=0 allocated=1
managed and free      managed=1 allocated=0
managed and allocated managed=1 allocated=1
```

It detects out-of-range frees, reserved-page frees, and double frees. Allocation
uses a rotating linear scan; a buddy allocator is not justified until contiguous
physical allocation becomes a real requirement.

## HHDM

Limine supplies a Higher Half Direct Map offset. A managed physical address `p`
is accessed by the kernel at:

```text
virtual = hhdm_offset + p
```

The physical allocator uses this mapping to clear newly allocated pages and to
edit page tables. This dependency belongs to the current Limine boot path; a
future Vostok loader must provide the same boot-info contract or construct an
equivalent direct map.

## x86-64 page tables

Vostok walks the four hardware levels explicitly:

```text
CR3 -> PML4 -> PDPT -> PD -> PT -> 4 KiB physical page
```

Each index uses nine bits of the virtual address. The final twelve bits are the
offset within a 4096-byte page. Current mappings use the `present`, `writable`,
`user`, and optional `no-execute` bits; huge pages are deliberately unsupported.

A new address space starts with an empty lower canonical half and copies the
upper-half PML4 entries installed by Limine. Consequently kernel code, the
current kernel stack, and HHDM remain reachable when CR3 changes, while future
userspace mappings stay private to the new root.

## Runtime self-test

At boot the kernel:

1. records the free-page count;
2. allocates a new PML4 and one data page;
3. maps the page at virtual address `0x400000`;
4. keeps `0x401000` absent as a guard page;
5. loads the new PML4 physical address into CR3;
6. writes and reads a known 64-bit value;
7. restores the original CR3;
8. unmaps and frees every allocated page-table/data page;
9. asserts that the free-page count returned to its original value.

Loading an invalid physical address into CR3 or losing the upper-half mappings
would fault before ordinary kernel code could continue and can escalate to a
triple fault. That is why the CR3 transition is exercised in QEMU on every
`make test`, rather than tested only through data-structure mocks.

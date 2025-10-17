# x86-64 diagnostic foundation

Vostok installs its own GDT, 64-bit TSS, and IDT before enabling any future
hardware interrupts. Limine's tables are treated only as boot-time state.

## Invariants

- selector `0x08` is the ring-0 64-bit code segment;
- selector `0x10` is the ring-0 data segment;
- selector `0x28` names one available 64-bit TSS;
- `TR` is loaded after `GDTR` and code/data segments are reloaded;
- TSS IST1 points to a dedicated 16 KiB double-fault stack;
- IDT vectors 0–31 are present ring-0 interrupt gates;
- vector 8 uses IST1 even when the interrupted kernel stack is damaged;
- every assembly stub normalises the frame to `vector, error, rip, cs, rflags`;
- ring-0 exceptions never return; ring-3 exceptions terminate only the current
  process and restore the next runnable process through `iretq`.

Some exceptions push a hardware error code and others do not. Assembly stubs
for exceptions without one push a zero placeholder. This lets C consume one
fixed prefix regardless of the vector. A privilege or IST stack switch also
adds the old `rsp` and `ss`; a same-privilege exception does not.

The exception test uses an address expected to be unmapped and verifies the
page-fault vector, saved registers, CR2 fault address, and decoded error bits.
An invalid IDT gate, selector, TSS descriptor, or frame layout can turn the
original fault into a double or triple fault, which is why the serial smoke test
is part of the milestone rather than an optional demonstration.

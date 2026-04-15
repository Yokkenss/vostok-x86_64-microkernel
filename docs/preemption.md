# Timer preemption

Vostok uses the x86 local APIC periodic timer on the bootstrap processor. The
kernel reads `IA32_APIC_BASE`, maps the APIC MMIO page only in the shared
kernel half, installs interrupt vector 32, and acknowledges each tick through
the APIC EOI register.

## Scheduling path

The interrupt stub saves the complete user register frame. The scheduler marks
the interrupted process ready, chooses the next ready process in PID order,
switches `CR3` and `TSS.RSP0`, and returns through the same `iretq` restore path
used by syscalls. Two CPU-bound Iskra programs make no syscalls; completing
both is therefore the regression proof that scheduling is truly preemptive.

User `RFLAGS` starts with IF set. A user exception kills only the current
process and records its vector and fault address. The identical exception in
ring 0 remains fatal because continuing after a kernel invariant failure would
hide corruption.

## Decision: local APIC instead of PIT

The legacy PIT/PIC path was considered first, but it couples the kernel to old
8259 routing and was not delivered reliably by the Q35 test machine. x2APIC
would avoid an MMIO mapping, but the QEMU/TCG CPU available in the development
environment did not expose it. The local APIC MMIO interface works on the
chosen machine and keeps interrupt acknowledgement local to the CPU.

Current limitations are deliberate: one core, a fixed periodic quantum, no
sleep queue, and no dynamic priority. SMP needs per-CPU scheduler state and
timer calibration rather than extending this single-core implementation with
hidden global assumptions.

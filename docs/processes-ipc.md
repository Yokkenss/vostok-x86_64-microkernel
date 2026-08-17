# Processes, IPC, and capabilities

This milestone adds a deliberately small microkernel execution model. Every
process has one thread, one private lower-half x86-64 address space, a guarded
user stack, a kernel stack selected through `TSS.RSP0`, and a fixed-size
capability table. Static ELF64 executables are supplied as Limine modules and
validated by the kernel before their loadable segments are mapped.

## Execution model

- User code runs at CPL 3 and enters the kernel through interrupt vector
  `0x80`. Syscalls, exceptions, and timer interrupts share one saved process
  context layout.
- Scheduling is preemptive, single-core round-robin. Local-APIC timer vector 32
  can switch a CPU-bound process that never invokes the kernel.
- User mappings are owned by exactly one process and are reclaimed, together
  with their page tables, after exit. The unmapped page below each user stack
  is its guard page.
- A lack of runnable processes while live processes remain is reported as an
  IPC deadlock instead of silently halting.

## Capability invariants

A handle contains a slot index and a generation. Lookup rejects empty slots,
stale generations, the wrong object type, and missing rights. Endpoint rights
are `SEND`, `RECEIVE`, and `TRANSFER`.

Capabilities are never ambient: the bootstrap code grants each process only
the handles it needs. An IPC transfer copies an endpoint capability into the
receiver's table. The requested rights must be a subset of the source rights,
so transfer can preserve or reduce authority but cannot amplify it. Closing a
handle advances its generation, invalidating stale copies of that handle.

The same typed table contains the shell's console object. Its `READ`, `WRITE`,
and `INSPECT` rights are checked on every console syscall. Passing an endpoint
handle to a console syscall is rejected as an object-type error.

## Demonstration

The boot image contains Iskra-generated user ELF modules. Six processes form
the boot regression workload; userspace `init` then starts the console and
interactive shell. Additional modules are created dynamically by `run`:

1. `sender` proves that its send-only handle cannot receive, then blocks while
   sending `42` and a send-only derivative of a transferable audit capability.
2. `auditor` waits on the audit endpoint.
3. `receiver` accepts the queued first message, then uses the received
   capability to deliver `99` directly to the waiting `auditor`.
4. `init` creates the initial ramfs source and starts system services.
5. `console` owns the UART device capability and serves an IPC endpoint.
6. `shell` provides file, editor, dynamic process and diagnostic commands; all
   character I/O crosses that endpoint.
7. `hello` and `iskra_vm` are not boot processes; `run` allocates a PID and
   isolated address space for them, then the shell waits for their exit.

This ordering exercises both sides of the rendezvous implementation: a sender
waiting for a later receiver and a sender finding a receiver already blocked.

`make test` checks the denial, both IPC deliveries, clean process exits, the
older memory and exception tests, and the Iskra frontend tests.

The userspace receive wrapper has an explicit typed destination:

```text
vostok_ipc_receive(endpoint: u64, out: *mut Message) -> i64
```

On success it writes the word and transferred capability into `Message` and
returns zero. A negative kernel error is returned without modifying `out`.

## Current limits

There is no SMP, demand paging, endpoint queue, revocation tree, or service
restart policy yet. Process creation loads bootfs ELF modules but does not yet
support dynamic linking. Endpoint objects are
kernel-owned bootstrap objects. These omissions keep the current object and
lifetime rules explicit enough to test before they are generalized.

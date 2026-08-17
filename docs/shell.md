# Iskra userspace shell

The boot image contains an interactive serial shell compiled from
`user/shell.isk` by the Iskra bootstrap compiler. A userspace `init` creates
the initial ramfs file and asks the kernel to start the console and shell;
command parsing and the editor do not run in the kernel.

After the background regression processes finish, the shell presents:

```text
Vostok 0.3
Type help for commands.
vostok>
```

Commands:

- `help` lists commands;
- `ps` asks the kernel for a read-only process snapshot;
- `hello` prints from Iskra userspace;
- `ls` lists read-only bootfs modules and writable ramfs files;
- `cat PATH` reads a file;
- `edit PATH` replaces a file and saves on Ctrl-D;
- `build PATH.isk` starts the native userspace compiler and writes
  `/home/a.out`;
- `run NAME` dynamically creates an ELF process from bootfs;
- `run PATH` loads an ELF from bootfs or writable ramfs;
- `run PATH.isk` remains a direct stage-0 source-runner convenience path;
- `metrics` reports live scheduler and IPC counters;
- `shutdown` returns from `main`, performs the normal process-exit syscall,
  and lets the kernel verify process outcomes and memory reclamation.

## Authority boundary

The console service receives a synchronous IPC endpoint and a UART object with
`READ | WRITE`. The shell receives the other endpoint handle and a separate
`INSPECT`-only diagnostic handle. It cannot invoke the UART syscalls directly.
Every device syscall validates the handle generation, object type, and required
right. The regression demo verifies that an endpoint handle is rejected when
used as a UART object.

The shell also receives separate process-spawn and filesystem capabilities.
Dynamic children receive only console and read-only filesystem authority. The
startup runtime keeps bootstrap handles in each process's private data page.
Output requests encode one character, and the service replies with an ACK only
after the UART write completes. Input uses request/reply IPC. A stop message
lets the service terminate normally when the shell shuts down.

## Receive interrupt path

Vostok maps the Q35 I/O APIC at `0xfec00000`, routes COM1 GSI 4 to IDT vector
33, enables the UART received-data interrupt, and acknowledges it through the
local APIC EOI register. The handler drains the FIFO into a bounded kernel ring
buffer and wakes a process blocked in the UART-read syscall. The software queue
holds 1024 bytes so pasted editor input can be absorbed without polling.

If every userspace process is blocked while waiting for input, the scheduler
executes `sti; hlt` instead of treating the state as IPC deadlock. Timer
interrupts that arrive in this ring-0 idle path only update/acknowledge the
timer; they do not attempt to save a kernel frame as a user process context.

## Current limitation

The protocol currently sends one character per synchronous IPC transaction and
supports one console endpoint. Receive is interrupt-driven, while transmit still
waits for the UART transmitter-ready bit. Batched buffers, multiple clients,
TX interrupts, and service restart policy remain future work.

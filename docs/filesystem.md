# Bootfs, ramfs, init and dynamic process creation

Vostok 0.3 exposes two deliberately small file namespaces:

- `/bin/*` is a read-only bootfs view of ELF modules supplied by Limine;
- `/home/*` is a writable in-memory filesystem with 16 files of at most 4096
  bytes each.

Ramfs data survives process exits but not a reboot. There are no directories,
permissions, deletion, rename, disk persistence, or concurrent file handles
yet. Paths are bounded byte strings rather than trusted kernel pointers.

## Startup

The kernel initially starts the regression tasks and `user/init.isk`. Init owns
an `INIT` process capability and a writable filesystem capability. It creates
`/home/main.isk`, then requests startup of the console and shell and exits.
The shell gets `SPAWN`, filesystem read/write, console IPC and diagnostic
capabilities; it never receives the UART object.

## Dynamic run

`run hello` looks up `/bin/hello`, validates and maps the ELF into a fresh
address space, allocates a new PID, and blocks the shell until that child exits.
Dead process slots and every owned page are reusable.

`build /home/main.isk` starts the Iskra-written `iskrac_native` process with a
read/write filesystem capability. It emits a complete ELF64 executable to
`/home/a.out`. `run /home/a.out` then follows the same validated ELF loading
path as bootfs programs. `run PATH.isk` remains a direct stage-0 runner for
comparison, but it is not the final demonstration path.

The native compiler is deliberately a bootstrap subset rather than the full
host compiler: it recognizes the first `print("...")` string, decodes escapes,
emits the ELF/program headers and writes x86-64 machine instructions directly.
It needs no assembler or linker inside Vostok. Extending its parser and backend
to the complete Iskra language is future language work, not an unreported
capability of this release.

## Safety boundaries

Every file and process syscall checks capability object type and rights. User
ranges are bounded, checked for overflow, restricted to the lower canonical
half and verified page-by-page before the kernel copies bytes. Fixed-size data
structures make exhaustion explicit through `NO_SPACE` rather than allocation
failure in an interrupt or syscall path.

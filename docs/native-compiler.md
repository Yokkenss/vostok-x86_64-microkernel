# Native Iskra bootstrap compiler

The final Vostok 0.3 demonstration has no host tool in its edit/build/run loop:

```text
vostok> edit /home/demo.isk
vostok> build /home/demo.isk
iskrac: native x86-64 ELF written to /home/a.out
vostok> run /home/a.out
```

`user/iskrac_native.isk` is itself compiled by the host bootstrap compiler into
a normal Vostok process. At runtime it reads source through its filesystem
capability and constructs an ELF64 image byte-by-byte in a fixed array.

The generated file contains an ELF header, one `PT_LOAD` program header, a
small x86-64 entry routine and the decoded string data. The entry routine uses
the console IPC capability passed in `rdi`; it has no UART authority. The
kernel validates the ramfs bytes with the same overflow, machine, segment and
entry checks used for boot modules before mapping them into a fresh address
space.

For compactness this bootstrap emits one read/write/execute load segment.
Separating code and data into W^X segments is deliberately left as a hardening
step; the current layout should not be mistaken for the final security model.

## Supported native subset

The bootstrap native compiler currently accepts a source containing one
non-empty string literal, conventionally:

```text
fn main() -> i32 { print("hello\\n"); return 0; }
```

It supports the string escapes needed by this form and a maximum decoded
message of 512 bytes. It does not yet reuse the full host lexer, type checker,
HIR or general expression backend. Calling it a complete self-hosted Iskra
compiler would therefore be inaccurate; it is a real native ELF-emitting
bootstrap compiler and a tested bridge toward that later goal.

## Security and authority

Only the compiler process receives filesystem write authority. The produced
program receives console IPC and read-only filesystem authority. The shell
waits for compiler and program exit, so output ordering and failure reporting
are deterministic. All temporary address spaces and physical pages are
reclaimed after the demo.

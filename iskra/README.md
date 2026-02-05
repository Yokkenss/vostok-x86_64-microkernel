# Iskra

Iskra is a small systems programming language being developed alongside the
Vostok microkernel. Its first target is `x86_64-unknown-vostok`; its first job
is to build small, statically linked userspace programs for Vostok.

The project started with a specification. That specification is now the
contract exercised by a small C bootstrap frontend.

## Milestone 1: language foundation

Status: **complete as a versioned draft**.

- lexical and grammatical subset defined;
- scalar types, pointers, functions and structures defined;
- mutation and conversion rules defined;
- failure behaviour and initial exclusions recorded;
- x86-64 calling convention and data layout drafted;
- valid and invalid normative examples added.

Documents:

- [Language specification](docs/language.md)
- [Vostok ABI](docs/abi.md)
- [Normative examples](examples)

## Milestone 2: bootstrap frontend

Status: **implemented**.

- lexer with source locations and comment support;
- recursive-descent declarations/statements parser;
- Pratt expression parser with documented precedence;
- deterministic S-expression AST dump;
- source-line diagnostics with stable `lex` and `parse` categories;
- positive and negative host tests.

Build and inspect a program:

```sh
make
build/iskrac --dump-tokens examples/minimal.isk
build/iskrac --dump-ast examples/minimal.isk
make test
```

## Milestone 3: typed HIR, bootstrap backend, and Vostok target

Status: **implemented for the current freestanding subset**.

- top-level function and constant name resolution;
- local scopes, mutability checks, typed structures and pointer checking;
- typed HIR side table with resolved binding IDs and `--emit-hir` dumps;
- function signature, condition and return-path validation;
- direct x86-64 GNU assembly generation with stable Iskra symbol mangling;
- stack locals, calls, branches, loops, wrapping fixed-width arithmetic and
  short-circuit boolean operations;
- naturally aligned structure layout, field access, address-of and pointer
  dereference in the bootstrap backend;
- Vostok startup and syscall bindings for IPC and capabilities;
- Iskra-generated ELF processes exercised by the root QEMU regression test.

Inspect generated code with:

```sh
build/iskrac --check examples/control_flow.isk
build/iskrac --emit-asm examples/control_flow.isk
build/iskrac --emit-hir examples/structures.isk
```

The direct backend is a bootstrap path, not the final compiler architecture.
The typed HIR is intentionally still a resolved tree rather than CFG/SSA.
CFG construction, SSA, optimisation and register allocation are later
milestones; structures are currently passed through explicit pointers rather
than by-value SysV aggregate classification.

## Design priorities

1. predictable semantics;
2. understandable compiler implementation;
3. useful diagnostics;
4. suitability for freestanding programs;
5. performance;

Iskra 0.1 is not memory-safe and does not claim to be. Pointer operations are
explicit, integer behaviour is defined, and undefined behaviour should be kept
to the smallest practical surface.

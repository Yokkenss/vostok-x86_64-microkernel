# Bootstrap x86-64 backend

The first code-generating Iskra compiler deliberately emits GNU assembly
directly from a semantically checked AST. This is a bootstrap backend: its job
is to establish the real `source -> Vostok ELF -> ring 3` path before an IR
pipeline is designed around imaginary optimisation requirements.

## Invariants

- Every expression leaves one normalized value in `rax`.
- Scalar parameters and locals use eight-byte stack slots. Structures use
  naturally aligned declaration-order layouts; narrow field loads and stores
  use their actual widths.
- `rsp` is 16-byte aligned before every call, including calls nested inside
  expressions with temporarily saved operands.
- Comparisons select signed or unsigned x86 conditions from the Iskra type.
- `&&` and `||` use branches and preserve short-circuit evaluation.
- Division uses hardware `div`/`idiv`; division faults therefore become the
  target's normal arithmetic trap. Oversized shifts execute `ud2` instead of
  inheriting x86's masked shift-count behaviour.
- Non-extern functions use the `_I<length><name>` ABI symbol. Extern symbols are
  emitted unchanged.

## Typed HIR and why not SSA yet?

Semantic analysis is followed by a typed HIR side table. Each lowered AST node
records its resolved type and, for names, a stable binding kind and ID. This
stops code generation from performing name resolution and supplies an
inspectable `--emit-hir` boundary without prematurely committing to CFG shape.

SSA becomes valuable after CFG construction and after more programs expose
useful optimisation cases. The direct backend remains a replaceable bootstrap
path while the real compiler pipeline is grown alongside it.

## Deliberate limits

The backend handles scalar integers, booleans, constants, direct calls, locals,
assignment, control flow, casts, structures and explicit pointers. Structures
cannot be passed or returned by value. Indirect calls, more than six arguments,
ELF object emission, debug information and register allocation are not
implemented.

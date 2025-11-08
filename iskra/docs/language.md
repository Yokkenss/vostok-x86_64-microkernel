# Iskra language draft 0.1

This document defines the first compiler-visible subset of Iskra. Anything not
described here is unsupported, even if an example might look familiar from C,
Rust, or another language.

## 1. Source model

- Source files are UTF-8.
- Draft 0.1 identifiers contain ASCII letters, digits, and `_` only.
- An identifier cannot begin with a digit.
- Keywords are lowercase and case-sensitive.
- Whitespace has no semantic meaning outside literals.
- `//` starts a line comment.
- `/* ... */` is a non-nesting block comment.
- One source file is one compilation unit. Modules and imports do not exist yet.

Keywords:

```text
as bool const else extern false fn if let mut null pub return
struct true var while void
```

## 2. Built-in types

```text
u8   u16   u32   u64
i8   i16   i32   i64
usize isize
bool
void
```

`usize` and `isize` are 64-bit on the initial x86-64 target. `void` is permitted
only as a function return type.

Integer representations are binary and two's complement. Arithmetic wraps to
the width of the result type. Division by zero and a shift by a value greater
than or equal to the operand width trap. Signed division overflow traps.

`bool` has exactly the values `true` and `false`. Integers are not implicitly
convertible to `bool`.

## 3. Literals

Draft 0.1 supports:

```text
42
0xff
0b1010
true
false
null
```

Decimal, hexadecimal, and binary integer literals are initially untyped. Their
value must fit the type required by context. With no expected type, an integer
literal defaults to `i64`.

`null` requires a pointer type from context. Character, string, byte-string,
array, and floating-point literals are postponed.

## 4. Variables and mutation

`let` creates an immutable local. `var` creates a mutable local. Both require an
initializer; their type may be written explicitly or inferred from it.

```text
let limit: u64 = 10;
let default_value = 0;  // i64
var index: u64 = 0;
index = index + 1;
```

Assignment to a `let` binding is a compile-time error. There is no uninitialised
local state in draft 0.1.

Top-level immutable constants use `const` and require an explicit type:

```text
const PAGE_SIZE: usize = 4096;
```

Constant expressions are restricted to literals and scalar operators whose
operands are constant expressions.

## 5. Functions

```text
fn add(left: u64, right: u64) -> u64 {
    return left + right;
}

fn halt() -> void {
    while true {
    }
}
```

Parameters are immutable. A non-`void` function must return a value on every
reachable path. Reaching the closing brace of a `void` function returns normally.

`pub` exports a function from the compilation unit. `extern` declares a function
whose symbol is supplied during linking and has no body:

```text
extern fn debug_value(value: u64) -> void;
pub fn main() -> i32 { return 0; }
```

Function overloading, default arguments, variadic functions, methods, closures,
and recursion checks are not part of draft 0.1. Ordinary recursion is allowed.

## 6. Control flow

Draft 0.1 has `if`, `else`, and `while`:

```text
if value == 0 {
    return 1;
} else {
    return value;
}

while index < limit {
    index = index + 1;
}
```

Conditions must have type `bool`. There are no implicit truthy values. `for`,
`break`, `continue`, `switch`, and pattern matching are postponed.

## 7. Operators

Operators, from highest to lowest precedence:

| Group | Operators |
|---|---|
| postfix | function call `()`, field access `.` |
| unary | `-`, `!`, `~`, `*`, `&`, `&mut` |
| multiplicative | `*`, `/`, `%` |
| additive | `+`, `-` |
| shift | `<<`, `>>` |
| relational | `<`, `<=`, `>`, `>=` |
| equality | `==`, `!=` |
| bitwise AND | `&` |
| bitwise XOR | `^` |
| bitwise OR | `|` |
| logical AND | `&&` |
| logical OR | `||` |

Binary operators associate left-to-right. Assignment is a statement, not an
expression. `&&` and `||` short-circuit.

Implicit numeric conversions do not exist. Explicit conversion uses `as`:

```text
let narrow: u8 = wide as u8;
```

Integer casts truncate or extend deterministically. A signed extension is used
only when the source type is signed. Pointer/integer casts are not supported in
the first compiler; they will require an explicit language decision.

## 8. Pointers

Pointer types are explicit:

```text
*const u8
*mut u64
```

- `&value` produces a `*const T`.
- `&mut variable` produces a `*mut T` and requires mutable storage.
- `*pointer` dereferences a pointer.
- writing through `*const T` is a compile-time error.
- dereferencing `null`, an invalid address, or a misaligned address traps at
  runtime when hardware detects it; draft 0.1 provides no pointer validity proof.

Pointer arithmetic is deliberately absent. Memory buffers will initially be
accessed through runtime functions until slice and array semantics are designed.

Iskra pointers are not Rust references: draft 0.1 has no ownership, lifetime,
or alias analysis.

## 9. Structures

```text
struct Point {
    x: i64,
    y: i64,
}

let origin: Point = Point { x: 0, y: 0 };
```

Fields are laid out in declaration order with ABI-required padding. Every field
must be initialised exactly once. Field names in an initializer may appear in
any order.

Draft 0.1 structures can be stored in locals and accessed through values or
pointers. They cannot yet be passed or returned by value; functions pass a
pointer instead. Unions, packed structures, bit fields, anonymous fields, and
generic structures are postponed.

## 10. Names and visibility

Top-level names share one namespace. Duplicate declarations are errors except
for one compatible `extern fn` declaration followed by its definition.

Names are private to the compilation unit unless declared `pub`. An `extern fn`
uses its written name as the external ELF symbol. Mangling for ordinary Iskra
functions is defined in the ABI document.

Shadowing a local name in an inner block is allowed. Redeclaring a name in the
same block is an error.

## 11. Grammar outline

This is the parser contract for the next milestone. `expression` follows the
precedence table above.

```text
file          := item* EOF
item          := const_decl | struct_decl | function_decl
const_decl    := "const" IDENT ":" type "=" expression ";"
struct_decl   := "struct" IDENT "{" field_decl* "}"
field_decl    := IDENT ":" type ","
function_decl := "pub"? "extern"? "fn" IDENT
                 "(" parameter_list? ")" "->" type
                 (block | ";")
parameter_list := parameter ("," parameter)* ","?
parameter     := IDENT ":" type
type          := scalar_type | IDENT | "*" ("const" | "mut") type
scalar_type   := "u8" | "u16" | "u32" | "u64"
               | "i8" | "i16" | "i32" | "i64"
               | "usize" | "isize" | "bool" | "void"
block         := "{" statement* "}"
statement     := local_decl | assignment | return_stmt | if_stmt
               | while_stmt | expression ";"
local_decl    := ("let" | "var") IDENT (":" type)? "=" expression ";"
assignment    := expression "=" expression ";"
return_stmt   := "return" expression? ";"
if_stmt       := "if" expression block ("else" (block | if_stmt))?
while_stmt    := "while" expression block
primary       := INTEGER | "true" | "false" | "null" | IDENT
               | struct_literal | "(" expression ")"
struct_literal := IDENT "{" field_init ("," field_init)* ","? "}"
field_init    := IDENT ":" expression
```

The parser may accept only syntactically valid assignment targets; otherwise
the semantic pass must reject them.

## 12. Required diagnostics

Every compiler error must include:

- file name;
- one-based line and column;
- a stable error category;
- a short explanation;
- the relevant source line and span where practical.

The compiler must reject a compilation unit after errors. It must never emit an
executable from an AST that failed name or type checking.

## 13. Explicit exclusions

Draft 0.1 does not include heap allocation, arrays, slices, strings, modules,
generics, interfaces, macros, inline assembly, concurrency, exceptions, object
orientation, garbage collection, ownership checking, floating point, or a
standard library. These require separate design decisions rather than accidental
syntax growth.

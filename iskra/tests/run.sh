#!/bin/sh
set -eu

compiler=$1
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

for source in examples/minimal.isk examples/control_flow.isk examples/structures.isk; do
    "$compiler" --dump-tokens "$source" >"$temporary/tokens"
    "$compiler" --dump-ast "$source" >"$temporary/ast"
    grep -F "1:1" "$temporary/tokens" >/dev/null
    grep -F "(program" "$temporary/ast" >/dev/null
    grep -F "(fn main public" "$temporary/ast" >/dev/null
done

"$compiler" --check examples/minimal.isk
"$compiler" --check examples/control_flow.isk
"$compiler" --check examples/structures.isk

"$compiler" --emit-asm tests/valid/backend_runtime.isk \
    >"$temporary/backend_runtime.S"
"$compiler" --emit-asm examples/control_flow.isk \
    >"$temporary/control_flow.S"
"$compiler" --emit-asm examples/structures.isk \
    >"$temporary/structures.S"
"$compiler" --emit-asm tests/valid/structure_layout.isk \
    >"$temporary/structure_layout.S"
cc -c tests/host_start.S -o "$temporary/host_start.o"
for program in backend_runtime control_flow structures structure_layout; do
    cc -c "$temporary/$program.S" -o "$temporary/$program.o"
    ld -static -e _start "$temporary/host_start.o" "$temporary/$program.o" \
        -o "$temporary/$program"
    "$temporary/$program"
done

"$compiler" --dump-ast examples/minimal.isk >"$temporary/minimal.ast"
cmp tests/expected/minimal.ast "$temporary/minimal.ast"

if "$compiler" --dump-ast tests/invalid/missing_semicolon.isk \
    >"$temporary/output" 2>"$temporary/error"; then
    echo "missing-semicolon fixture unexpectedly parsed" >&2
    exit 1
fi
grep -F "tests/invalid/missing_semicolon.isk:3:5: error[parse]" \
    "$temporary/error" >/dev/null
grep -F "^" "$temporary/error" >/dev/null

if "$compiler" --dump-tokens tests/invalid/bad_character.isk \
    >"$temporary/output" 2>"$temporary/error"; then
    echo "bad-character fixture unexpectedly lexed" >&2
    exit 1
fi
grep -F "tests/invalid/bad_character.isk:2:12: error[lex]" \
    "$temporary/error" >/dev/null

if "$compiler" --check examples/invalid/immutable_assignment.isk \
    >"$temporary/output" 2>"$temporary/error"; then
    echo "immutable assignment unexpectedly type checked" >&2
    exit 1
fi
grep -F "error[semantic]: cannot assign to an immutable binding" \
    "$temporary/error" >/dev/null

if "$compiler" --check tests/invalid/unknown_name.isk \
    >"$temporary/output" 2>"$temporary/error"; then
    echo "unknown name unexpectedly type checked" >&2
    exit 1
fi
grep -F "error[semantic]: use of an unknown name" "$temporary/error" \
    >/dev/null

if "$compiler" --check tests/invalid/missing_return.isk \
    >"$temporary/output" 2>"$temporary/error"; then
    echo "missing return unexpectedly type checked" >&2
    exit 1
fi
grep -F "error[semantic]: non-void function does not return on every path" \
    "$temporary/error" >/dev/null

echo "Iskra compiler tests passed"

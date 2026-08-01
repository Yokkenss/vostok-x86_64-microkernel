#!/bin/sh
set -eu

if [ -n "${QEMU_INPUT_BASE64:-}" ]; then
    QEMU_INPUT=$(printf '%s' "$QEMU_INPUT_BASE64" | base64 -d)
fi

image=$1
shift
output=$(mktemp)
trap 'rm -f "$output"' EXIT

set +e
if [ -n "${QEMU_INPUT:-}" ]; then
    (sleep "${QEMU_INPUT_DELAY:-2}"; printf '%b' "$QEMU_INPUT") | \
        timeout "${QEMU_TIMEOUT:-7}s" qemu-system-x86_64 \
        -M q35 -m "${QEMU_MEMORY:-128M}" -cdrom "$image" -boot d \
        -display none -serial stdio -monitor none -no-reboot >"$output" 2>&1
else
    timeout "${QEMU_TIMEOUT:-5}s" qemu-system-x86_64 \
        -M q35 -m "${QEMU_MEMORY:-128M}" -cdrom "$image" -boot d \
        -display none -serial stdio -monitor none -no-reboot >"$output" 2>&1
fi
status=$?
set -e

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    cat "$output"
    echo "QEMU exited unexpectedly with status $status" >&2
    exit 1
fi

for expected in "$@"; do
    if ! grep -F "$expected" "$output" >/dev/null; then
        cat "$output"
        echo "missing serial output: $expected" >&2
        exit 1
    fi
done

cat "$output"

#!/bin/sh
set -eu

image=$1
compiler=$2
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

iterations=20
start=$(date +%s%N)
index=0
while [ "$index" -lt "$iterations" ]; do
    "$compiler" --emit-asm iskra/examples/control_flow.isk \
        >"$temporary/program-$index.S"
    index=$((index + 1))
done
end=$(date +%s%N)
elapsed_ns=$((end - start))

echo "Iskra compiler: $iterations control_flow builds, $((elapsed_ns / 1000000)) ms total, $((elapsed_ns / iterations / 1000)) us average"
echo "Artifact sizes (bytes):"
printf '  %-24s %s\n' iskrac "$(stat -c %s "$compiler")"
printf '  %-24s %s\n' vostok.elf "$(stat -c %s build/vostok.elf)"
for program in build/user/*.elf; do
    printf '  %-24s %s\n' "$(basename "$program")" "$(stat -c %s "$program")"
done

set +e
(sleep 2; printf 'metrics\rshutdown\r') | \
timeout 7 qemu-system-x86_64 -M q35 -m 128M -cdrom "$image" -boot d \
    -display none -serial stdio -monitor none -no-reboot \
    >"$temporary/qemu.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    cat "$temporary/qemu.log" >&2
    exit "$status"
fi
grep -E 'scheduler metrics:|scheduler switch cost:|IPC metrics:|UART metrics:|process memory reclaimed:' \
    "$temporary/qemu.log"

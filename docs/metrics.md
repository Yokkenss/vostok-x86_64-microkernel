# Reproducible metrics

Run `make metrics` after installing the normal build dependencies. It reports:

- average Iskra bootstrap compiler time across 20 identical builds;
- byte size of the compiler, kernel ELF, and every userspace ELF;
- timer ticks, preemptions, context switches, and measured scheduler cycles;
- IPC delivery count and min/average/max rendezvous latency in TSC cycles;
- delivered UART receive-interrupt count;
- physical pages available after all process address spaces are reclaimed.

Cycle values are intended for regression comparison on the same QEMU and host
configuration, not as hardware-independent nanoseconds. QEMU scheduling and
host load introduce noise, so changes should be compared over several runs.

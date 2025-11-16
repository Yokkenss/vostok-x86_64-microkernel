# Roadmap Vostok и Iskra

Этот план рассчитан примерно на 30 месяцев активной разработки с возможным
расширением до 36 месяцев. Он не является обещанием конкретных дат: переход к
следующей фазе происходит только после выполнения проверяемого Definition of
Done текущей фазы.

## Замысел

Мы развиваем две связанные системы:

- **Vostok** — capability-based x86-64 microkernel и минимальные userspace-сервисы;
- **Iskra** — небольшой системный язык и постепенно self-hosting компилятор.

В начале используются только инфраструктурные готовые компоненты: GCC/binutils,
GNU Make, Limine, QEMU, GDB и xorriso. Ядро не использует libc. Затем Limine и
части toolchain по одной заменяются собственными реализациями.

QEMU, GDB, Git и редактор не требуется заменять: это лабораторные инструменты,
а не части поставляемой ОС. Эталонная сборка через Limine сохраняется даже после
появления собственного загрузчика — она нужна как независимый путь диагностики.

## Правила развития

1. В каждый момент существует загружаемый и диагностируемый end-to-end путь.
2. Одновременно заменяется не более одного критического готового компонента.
3. Новый собственный компонент сначала работает рядом с эталонным, затем
   сравнивается с ним и только после этого становится основным.
4. Возможность объяснить механизм важнее количества возможностей.
5. Каждый milestone завершается документацией invariants, тестом и демонстрацией.
6. Kernel ABI и Iskra ABI версионируются; изменения не вносятся молча.
7. Scope проекта остаётся узким: один CPU, статическая линковка, QEMU и небольшой
   набор устройств до появления стабильной userspace-вертикали.

## Общая зависимость проектов

```text
Vostok: boot -> diagnostics -> memory -> threads -> ring 3 -> IPC/caps
                                                        |
                                                        v
Iskra:  spec -> C bootstrap compiler -> Vostok target -> native compiler
                                                        |
                                                        v
Vostok: init/services <- Iskra runtime <- Iskra programs
```

Iskra не используется в ядре, пока компилятор и ABI не стабилизированы. Сначала
он применяется для маленьких userspace-программ: их ошибка не должна разрушать
kernel.

## Фаза 0: первая загрузка (готово)

**Срок:** недели 1–2.

- freestanding ELF64 kernel;
- загрузка через закреплённую версию Limine;
- точка входа `_start` и переход в `kernel_main`;
- COM1 diagnostics;
- безопасный halt loop;
- BIOS- и UEFI-совместимый ISO;
- воспроизводимые `make` и `make run`.

**Definition of Done:** QEMU печатает `Vostok` и `kernel started`; ELF не имеет
неразрешённых символов.

## Фаза 1: диагностируемое ядро

**Срок:** месяцы 1–2. **Фокус:** Vostok 90%, Iskra 10%.

**Статус:** реализовано; проверяется общей целью `make test`.

Vostok:

- собственные `memset`, `memcpy` и минимальное форматирование без libc;
- `panic`, assertions и serial log levels;
- GDT, TSS и IDT;
- stubs для всех x86-64 exceptions;
- register dump для `#PF`, `#GP`, `#UD` и double fault;
- повторяемый QEMU smoke test.

Iskra:

- документ `language.md`: цели и явно исключённые возможности;
- целочисленные типы фиксированной ширины, указатели, функции и структуры;
- черновик calling convention, совместимый с Vostok ABI;
- несколько маленьких программ, определяющих желаемый синтаксис.

**Definition of Done:** намеренный page fault выдаёт vector, error code, `rip`,
`rsp`, `rflags` и `cr2`, после чего система останавливается предсказуемо.

## Фаза 2: память

**Срок:** месяцы 2–4. **Фокус:** Vostok 80%, Iskra 20%.

**Статус Vostok:** реализовано; allocator metadata проверяется на host, а CR3
self-test выполняется в QEMU. Iskra-часть этой временной фазы остаётся отдельной.

Vostok:

- разбор memory map от Limine;
- physical page allocator с проверяемым учётом страниц;
- явное представление PML4/PDPT/PD/PT и флагов x86-64;
- kernel virtual memory mappings;
- создание и уничтожение отдельного address space;
- guard pages и диагностика page faults;
- host-тесты allocator metadata.

Iskra:

- lexer и parser bootstrap-компилятора на C;
- AST dump и точные source diagnostics;
- отсутствие оптимизатора и package manager.

**Definition of Done:** allocator проходит host-тесты, а kernel создаёт тестовый
address space, отображает страницу, проверяет содержимое и полностью освобождает
ресурсы.

## Фаза 3: выполнение и userspace

**Срок:** месяцы 4–6. **Фокус:** Vostok 75%, Iskra 25%.

Vostok:

- kernel threads и явный interrupt/context frame;
- cooperative round-robin, затем timer preemption;
- локальный APIC timer или минимальный выбранный timer path;
- ring 3, TSS/RSP0 и отдельные user address spaces;
- минимальный `SYSCALL/SYSRET` либо interrupt-gate syscall path после сравнения;
- статический ELF64 loader для одного userspace image.

Iskra:

- name resolution и простой type checker;
- C bootstrap-компилятор генерирует x86-64 GNU assembly;
- первые freestanding функции без runtime и allocation;
- golden tests: source -> AST/diagnostic/assembly.

**Definition of Done:** две изолированные user-программы выполняются по очереди,
не могут читать память друг друга и возвращаются в kernel через syscall.

## Фаза 4: microkernel vertical slice

**Срок:** месяцы 6–9. **Фокус:** Vostok 70%, Iskra 30%.

Vostok:

- typed kernel objects и стабильная схема lifetime;
- capability table на процесс;
- права, generation counters, copy/move/revoke semantics;
- synchronous IPC endpoint;
- передача ограниченного набора capabilities через IPC;
- root task получает только явно выданные начальные capabilities;
- базовый userspace console service.

Iskra:

- Vostok ABI и syscall bindings;
- минимальный runtime без heap;
- первая Iskra-программа печатает через console service, а не напрямую в COM1;
- compile-fail тесты для type system.

**Definition of Done:** Iskra client отправляет сообщение userspace console
server через capability-controlled IPC; kernel не содержит console policy.

## Фаза 5: маленькая работающая система

**Срок:** месяцы 9–12. **Фокус:** Vostok 60%, Iskra 40%.

Vostok:

- init/root task и manifest начальных объектов;
- initramfs как простой read-only источник ELF и данных;
- userspace process manager без `fork`;
- userspace ramfs service;
- bounded IPC payloads и timeout/cancellation policy;
- fault isolation и controlled process termination;
- простая shell как IPC client.

Iskra:

- модули, `enum`, control flow и явная обработка ошибок;
- небольшой allocator/runtime в userspace;
- bindings для IPC, memory и process protocols;
- shell и минимум один service написаны на Iskra.

**Год 1 завершён, если:** после `make run` стартуют kernel, root task, console
service и shell; shell запускает статическую Iskra-программу из initramfs.

## Фаза 6: собственная загрузочная цепочка

**Срок:** месяцы 13–18. **Фокус:** Vostok/boot 60%, Iskra 40%.

Сначала создаётся отдельный `vostok-loader`, не меняющий kernel ABI:

1. BIOS boot sector и stage 2;
2. чтение заранее определённого disk layout;
3. A20, GDT и переход в protected mode;
4. собственный минимальный ELF64 loader;
5. построение начальных page tables;
6. включение PAE, `EFER.LME`, paging и переход в long mode;
7. формирование versioned boot-info structure;
8. переход на `_start` ядра.

После BIOS пути добавляется небольшой UEFI loader, использующий UEFI только для
получения memory map, чтения kernel image и `ExitBootServices`.

**Definition of Done:** один и тот же kernel image загружается через Limine,
собственный BIOS loader и собственный UEFI loader; общий kernel smoke test
проходит во всех трёх режимах.

## Фаза 7: зрелый Iskra toolchain

**Срок:** месяцы 16–22, параллельно фазе 6.

- фиксированная grammar и versioned language specification;
- IR между type checker и backend;
- простой register allocation без сложной оптимизации;
- прямой вывод ELF64 relocatable objects вместо текстового assembly;
- debug line information минимального объёма;
- compiler test suite и differential tests со старым assembly backend;
- начало переписывания компилятора с C на подмножество Iskra.

**Definition of Done:** оба backend дают одинаковое наблюдаемое поведение тестов,
а собственный `.o` принимается эталонным GNU linker.

## Фаза 8: native и self-hosting Iskra

**Срок:** месяцы 21–27. **Фокус:** Vostok 40%, Iskra 60%.

Vostok:

- filesystem protocol, достаточный для исходников и build artifacts;
- demand paging только при доказанной необходимости;
- стабильные process, memory, file и console protocols;
- kernel ABI freeze на время bootstrap.

Iskra:

- compiler core переписан на Iskra;
- stage 0 (C) собирает stage 1 (Iskra);
- stage 1 собирает stage 2;
- stage 1 и stage 2 дают идентичный либо семантически эквивалентный результат;
- compiler запускается как обычный процесс Vostok;
- сборка небольшой программы непосредственно внутри Vostok.

**Definition of Done:** Vostok загружает исходник Iskra, native compiler создаёт
исполняемый файл, process manager запускает его, а результат проходит тест.

## Фаза 9: замена binutils в основном пути

**Срок:** месяцы 25–30.

- собственная библиотека чтения/записи ELF;
- статический `iskra-ld` с минимальным набором relocations;
- собственный узкий assembler только если он ещё нужен backend/tooling;
- детерминированный image manifest и build orchestrator;
- GNU binutils остаётся oracle в тестах, но исчезает из основного Iskra path;
- reproducibility test: две чистые сборки дают одинаковые artifacts.

**Definition of Done:** Iskra source превращается в Vostok ELF без GCC, GNU `as`
и GNU `ld`; bootstrap C compiler остаётся только recovery-инструментом.

## Фаза 10: укрепление и реальное железо

**Срок:** месяцы 30–36, опциональное продолжение.

- один тщательно выбранный путь реального железа;
- ACPI discovery и минимальный набор необходимых драйверов;
- serial и framebuffer console services;
- fuzz/property tests для ELF, IPC и capability operations;
- recovery image и documented debugging workflow;
- первая versioned демонстрационная release.

Сеть, USB, GUI, SMP, dynamic linking и POSIX добавляются только отдельными
решениями после стабильной однопроцессорной системы.

## Линия замены готовых компонентов

| В начале | Собственная замена | Когда |
|---|---|---|
| Limine | `vostok-loader` BIOS и UEFI | фаза 6 |
| GCC для kernel | Iskra для userspace; kernel остаётся C до отдельного решения | фазы 4–9 |
| GCC для bootstrap compiler | self-hosting Iskra compiler | фаза 8 |
| GNU assembler | прямой ELF backend или узкий Iskra assembler | фазы 7–9 |
| GNU linker | `iskra-ld` | фаза 9 |
| GNU Make | небольшой manifest/build orchestrator, если это оправдано | фаза 9 |
| xorriso | можно оставить packaging tool; замена не даёт знаний об OS | не приоритет |
| QEMU/GDB | остаются лабораторными инструментами | постоянно |

## Ритм работы

Для одного разработчика разумно держать не больше двух активных задач:

- одна вертикальная задача Vostok;
- одна небольшая задача Iskra, не зависящая от незаконченного kernel API.

Рекомендуемый цикл milestone:

```text
hardware/language question
-> короткая design note
-> минимальная реализация
-> host test, если возможен
-> QEMU integration test
-> негативный тест
-> документирование invariants
-> небольшой commit
```

Каждые 4–6 недель roadmap пересматривается по фактическим результатам. Сроки
двигаются, но Definition of Done не размывается ради красивого процента готовности.

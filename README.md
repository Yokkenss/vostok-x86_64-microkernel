# vostok and iskra

vostok is a capability based x86-64 operating system developed together with the iskra systems programming language

the project demonstrates a complete path from source code to an isolated native userspace process

the iskra compiler turns source code into an x86-64 elf file and vostok loads that file as a ring 3 process

## current status

vostok 0 3 is a reproducible research operating system prototype

iskra 0 1 is a small statically typed systems language with a host bootstrap compiler and a native compiler bootstrap inside vostok

the system currently provides

- limine bios and uefi boot
- a freestanding x86-64 kernel
- physical and four level virtual memory management
- private process address spaces and guarded user stacks
- ring 3 execution and static elf64 loading
- local apic timer preemption
- a single core round robin scheduler
- synchronous ipc with capability transfer
- typed capabilities with generation checked handles and restricted rights
- a userspace init process and uart console service
- read only bootfs and writable in memory ramfs
- an interactive shell and text editor
- dynamic process creation and waiting
- scheduler ipc uart and memory metrics
- host tests and qemu regression tests
- userspace programs written in iskra

## visible demo

the main demo runs completely inside vostok after boot

```
vostok> edit /home/demo.isk
fn main() -> i32 { print("hello from native iskra\n"); return 0; }

vostok> build /home/demo.isk
iskrac: native x86-64 elf written to /home/a.out

vostok> run /home/a.out
hello from native iskra
```

the editor saves on ctrl d

the build command starts an iskra written compiler process that reads the source through a filesystem capability and emits elf64 without an assembler or linker inside vostok

the run command validates the elf maps it into a fresh address space and starts it in ring 3

## shell commands

```
help
ps
hello
ls
cat path
edit path
build path.isk
run name
run path
metrics
shutdown
```

- `help` shows the command list
- `ps` shows process identifiers names and states
- `hello` prints from an iskra userspace program
- `ls` lists bootfs modules and ramfs files
- `cat path` reads a file
- `edit path` creates or replaces a file
- `build path.isk` compiles a source file to `/home/a.out`
- `run name` loads a program from bootfs
- `run path` loads an elf from bootfs or ramfs
- `run path.isk` uses the stage zero source runner
- `metrics` prints live kernel measurements
- `shutdown` stops userspace and verifies cleanup

## architecture

limine loads the vostok kernel and the kernel starts the iskra userspace services

the kernel contains exception handling syscalls memory management processes scheduling the elf loader ipc capabilities bootfs and ramfs

userspace contains init the console service the shell the editor the native compiler and test programs

### kernel foundation

vostok installs its own gdt tss idt exception handlers syscall gate local apic timer and uart interrupt path

the physical allocator manages 4096 byte pages from limine usable ranges and detects invalid frees reserved frees and double frees

every process owns a private lower half address space while the kernel and higher half direct map remain shared in the upper half

process pages page tables and stacks are reclaimed after exit

each process has one thread one guarded user stack one kernel stack and a fixed capability table

the local apic timer preempts cpu bound programs even when they never enter the kernel

### ipc and capabilities

ipc uses synchronous rendezvous endpoints with one 64 bit word and optional capability transfer

handles encode a slot and generation while every lookup checks the object type and required rights

transferred rights can be preserved or reduced but never amplified

the shell cannot access uart directly and communicates with the console service through ipc

### files and programs

`/bin` exposes limine modules as read only bootfs

`/home` is writable ramfs with 16 files and a 4096 byte limit per file

static elf64 programs can be loaded from either namespace

ramfs contents survive process exit but not reboot

## iskra language

the host bootstrap compiler supports

- integers booleans and explicit casts
- immutable `let` and mutable `var` bindings
- functions constants and external functions
- `if` `else` and `while`
- structures with naturally aligned layout
- explicit constant and mutable pointers
- string literals and escapes
- fixed arrays with runtime bounds traps
- lexical syntactic name and type diagnostics
- deterministic token ast and typed hir dumps
- direct gnu x86-64 assembly generation
- the `x86_64-unknown-vostok` abi

the host compiler reads source with the lexer and parser then runs semantic analysis and creates typed hir

the backend creates x86-64 assembly which is assembled and linked into a vostok elf file

the native compiler inside vostok is intentionally smaller than the host compiler

it accepts a bootstrap program with one nonempty printed string and emits a real elf64 file to `/home/a.out`

it proves the in system edit build run path but is not yet a complete self hosted compiler

## building and running

the supported development environment is linux or wsl with gcc binutils make git curl tar xorriso and qemu system x86

```sh
make
make run
```

the boot image is created at `build/vostok.iso`

vostok uses the qemu serial console in the current configuration

## testing and metrics

```sh
make test
make metrics
```

the tests cover memory management address space switching exceptions ring 3 execution process cleanup preemption ipc capability denial capability transfer init console shell ramfs editing dynamic elf loading the complete edit build run scenario and the iskra frontend and backend

metrics report compiler timing binary sizes context switches preemptions scheduler cycles ipc latency uart interrupts and reclaimed pages

## documentation

- [memory management](docs/memory.md)
- [processes ipc and capabilities](docs/processes-ipc.md)
- [preemption](docs/preemption.md)
- [filesystem and init](docs/filesystem.md)
- [shell and console](docs/shell.md)
- [native compiler bootstrap](docs/native-compiler.md)
- [diagnostics](docs/diagnostics.md)
- [metrics](docs/metrics.md)
- [iskra language](iskra/docs/language.md)
- [iskra abi](iskra/docs/abi.md)
- [iskra backend](iskra/docs/backend.md)

## deliberate limits

vostok does not yet provide smp threads demand paging copy on write networking usb graphics persistent storage dynamic linking posix or production hardening

ramfs has no real directories deletion rename permissions or disk persistence

the native compiler emits one read write execute segment and supports only its documented bootstrap subset

iskra does not yet provide cfg ssa optimizations register allocation modules generics slices heap allocation ownership floating point concurrency or a standard library

the project is a working vertically integrated research system rather than a general purpose operating system

---

# vostok и iskra

vostok это capability based операционная система для x86-64 которая разрабатывается вместе с системным языком iskra

проект демонстрирует полный путь от исходного кода до изолированного нативного пользовательского процесса

компилятор iskra превращает исходный код в файл elf для x86-64 а vostok загружает этот файл как процесс ring 3

## текущее состояние

vostok 0 3 это воспроизводимый исследовательский прототип операционной системы

iskra 0 1 это небольшой статически типизированный системный язык с основным bootstrap компилятором на host и нативным bootstrap компилятором внутри vostok

сейчас система предоставляет

- загрузку через limine в режимах bios и uefi
- freestanding ядро x86-64
- управление физической и четырёхуровневой виртуальной памятью
- отдельные адресные пространства процессов и пользовательские стеки с guard page
- выполнение в ring 3 и загрузку статических elf64
- вытеснение через таймер local apic
- однопроцессорный round robin планировщик
- синхронный ipc с передачей capabilities
- типизированные capabilities с поколениями handles и ограниченными правами
- пользовательские init и консольный сервис uart
- read only bootfs и записываемую ramfs в памяти
- интерактивный shell и текстовый редактор
- динамическое создание процессов и ожидание завершения
- метрики планировщика ipc uart и памяти
- host тесты и qemu regression тесты
- пользовательские программы написанные на iskra

## видимая демонстрация

главная демонстрация полностью выполняется внутри vostok после загрузки

```
vostok> edit /home/demo.isk
fn main() -> i32 { print("привет из нативной iskra\n"); return 0; }

vostok> build /home/demo.isk
iskrac: native x86-64 elf written to /home/a.out

vostok> run /home/a.out
привет из нативной iskra
```

редактор сохраняет файл по ctrl d

команда build запускает отдельный процесс компилятора написанный на iskra который читает исходник через файловую capability и создаёт elf64 без ассемблера или линкера внутри vostok

команда run проверяет elf отображает его в новое адресное пространство и запускает в ring 3

## команды shell

```
help
ps
hello
ls
cat путь
edit путь
build путь.isk
run имя
run путь
metrics
shutdown
```

- `help` показывает список команд
- `ps` показывает идентификаторы имена и состояния процессов
- `hello` печатает сообщение из пользовательской программы iskra
- `ls` показывает модули bootfs и файлы ramfs
- `cat путь` читает файл
- `edit путь` создаёт или заменяет файл
- `build путь.isk` компилирует исходник в `/home/a.out`
- `run имя` загружает программу из bootfs
- `run путь` загружает elf из bootfs или ramfs
- `run путь.isk` использует упрощённый stage zero обработчик исходника
- `metrics` выводит текущие измерения ядра
- `shutdown` останавливает userspace и проверяет очистку ресурсов

## архитектура

limine загружает ядро vostok а ядро запускает пользовательские сервисы написанные на iskra

ядро содержит обработку исключений системные вызовы управление памятью процессы планировщик загрузчик elf ipc capabilities bootfs и ramfs

пользовательское пространство содержит init консольный сервис shell редактор нативный компилятор и тестовые программы

### основа ядра

vostok устанавливает собственные gdt tss idt обработчики исключений syscall gate таймер local apic и путь прерываний uart

физический аллокатор управляет страницами по 4096 байт из usable областей limine и обнаруживает неправильное освобождение reserved страниц и double free

каждый процесс получает отдельную нижнюю половину адресного пространства а ядро и higher half direct map остаются общими в верхней половине

страницы процессов таблицы страниц и стеки освобождаются после завершения

каждый процесс имеет один поток пользовательский стек с guard page kernel stack и фиксированную таблицу capabilities

таймер local apic вытесняет cpu bound программы даже когда они не входят в ядро

### ipc и capabilities

ipc использует синхронные rendezvous endpoints с одним 64 битным словом и необязательной передачей capability

handles кодируют слот и поколение а каждый поиск проверяет тип объекта и необходимые права

передаваемые права можно сохранить или уменьшить но нельзя усилить

shell не может обращаться к uart напрямую и общается с консольным сервисом через ipc

### файлы и программы

`/bin` предоставляет модули limine как read only bootfs

`/home` является записываемой ramfs на 16 файлов с ограничением 4096 байт на файл

статические программы elf64 могут загружаться из обоих пространств

данные ramfs сохраняются после завершения процессов но теряются после перезагрузки

## язык iskra

основной host bootstrap компилятор поддерживает

- целые числа логические значения и явные преобразования
- неизменяемые `let` и изменяемые `var` bindings
- функции константы и внешние функции
- `if` `else` и `while`
- структуры с естественным выравниванием
- явные константные и изменяемые указатели
- строковые литералы и escapes
- фиксированные массивы с runtime bounds traps
- лексические синтаксические именные и типовые диагностики
- детерминированный вывод tokens ast и typed hir
- прямую генерацию gnu assembly для x86-64
- abi `x86_64-unknown-vostok`

host компилятор читает исходник через lexer и parser затем выполняет semantic analysis и создаёт typed hir

backend создаёт assembly для x86-64 которая собирается и связывается в elf файл для vostok

нативный компилятор внутри vostok намеренно меньше основного host компилятора

он принимает bootstrap программу с одной непустой печатаемой строкой и создаёт настоящий elf64 в `/home/a.out`

он доказывает внутренний путь edit build run но пока не является полностью self hosted компилятором

## сборка и запуск

поддерживаемая среда разработки это linux или wsl с gcc binutils make git curl tar xorriso и qemu system x86

```sh
make
make run
```

загрузочный образ создаётся по адресу `build/vostok.iso`

в текущей конфигурации vostok использует последовательную консоль qemu

## тестирование и метрики

```sh
make test
make metrics
```

тесты проверяют управление памятью переключение адресного пространства исключения выполнение в ring 3 очистку процессов вытеснение ipc запреты capability модели передачу capabilities init console shell редактирование ramfs динамическую загрузку elf полный сценарий edit build run и frontend и backend языка iskra

метрики показывают время компиляции размеры бинарных файлов переключения контекста вытеснения cycles планировщика latency ipc прерывания uart и освобождённые страницы

## документация

- [управление памятью](docs/memory.md)
- [процессы ipc и capabilities](docs/processes-ipc.md)
- [вытеснение](docs/preemption.md)
- [файловая система и init](docs/filesystem.md)
- [shell и консоль](docs/shell.md)
- [нативный bootstrap компилятор](docs/native-compiler.md)
- [диагностика](docs/diagnostics.md)
- [метрики](docs/metrics.md)
- [язык iskra](iskra/docs/language.md)
- [abi iskra](iskra/docs/abi.md)
- [backend iskra](iskra/docs/backend.md)

## сознательные ограничения

vostok пока не предоставляет smp потоки demand paging copy on write сеть usb графику постоянное хранилище dynamic linking posix и production hardening

ramfs не имеет настоящих директорий удаления переименования permissions и сохранения на диске

нативный компилятор создаёт один read write execute сегмент и поддерживает только документированный bootstrap subset

iskra пока не предоставляет cfg ssa оптимизации register allocation модули generics slices heap allocation ownership floating point concurrency и стандартную библиотеку

проект является работающей вертикально интегрированной исследовательской системой а не универсальной операционной системой

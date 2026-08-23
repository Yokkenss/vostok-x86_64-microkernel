# iskra

iskra is a small systems language for vostok

the compiler is still a bootstrap compiler but it already builds the userspace programs used by the operating system

## language

iskra supports

- signed and unsigned integers from 8 to 64 bits
- `usize` `isize` `bool` and `void`
- immutable `let` values
- mutable `var` values
- constants
- functions and external functions
- local scopes
- `if` `else` and `while`
- arithmetic comparisons bit operations and explicit casts
- structures with aligned fields
- constant and mutable pointers
- string literals with escapes
- fixed arrays with bounds traps

the full supported syntax is described in [the language document](docs/language.md)

iskra is not memory safe

raw pointer validity is the responsibility of the program

## compiler

the host compiler is written in c

it has these main stages

```text
source
lexer
parser
ast
semantic analysis
typed hir
direct x86-64 assembly backend
```

the lexer tracks file line and column information

the parser uses recursive descent for statements and a pratt parser for expressions

semantic analysis checks names types mutability calls conditions and return paths

typed hir stores resolved bindings and expression types

the backend emits gnu assembly and the host build uses gnu assembler and ld to create a static elf64 file

## build

```sh
make
```

the compiler is created at `build/iskrac`

## commands

show tokens

```sh
build/iskrac --dump-tokens examples/minimal.isk
```

show the ast

```sh
build/iskrac --dump-ast examples/minimal.isk
```

check a program

```sh
build/iskrac --check examples/control_flow.isk
```

show typed hir

```sh
build/iskrac --emit-hir examples/structures.isk
```

emit assembly

```sh
build/iskrac --emit-asm examples/control_flow.isk
```

## diagnostics

compiler errors include

- file name
- line
- column
- error category
- a short message
- the related source line when available

the stable categories are `lex` `parse` and `semantic`

the compiler does not emit code after a failed semantic check

## abi

the target is `x86_64-unknown-vostok`

iskra uses the integer part of the system v amd64 calling convention

the first arguments use `rdi` `rsi` `rdx` `rcx` `r8` and `r9`

scalar return values use `rax`

the stack is aligned to 16 bytes before a call

the target does not use the red zone

ordinary iskra functions have deterministic symbol mangling

external functions keep their source name

more details are in [the abi document](docs/abi.md)

## vostok support

the root build uses iskra to compile init shell console and the other userspace programs

the generated programs are static elf64 files loaded by the vostok kernel

the runtime contains assembly wrappers for ipc capabilities files process control console access and process exit

vostok also has a smaller native compiler written in iskra

that compiler runs in ring 3 and writes `/home/a.out` directly to ramfs

it currently supports one printed string and is not the full host compiler

## tests

```sh
make test
```

the tests cover lexer parser semantic checks diagnostics structures pointers strings arrays control flow arithmetic and generated machine code

the root vostok test also runs iskra programs in qemu

## missing parts

the compiler does not have cfg ssa optimization passes machine ir register allocation direct object output or a native linker

the language does not have modules imports generics slices dynamic arrays a heap ownership floating point concurrency exceptions macros or a standard library

---

# iskra

iskra это небольшой системный язык для vostok

компилятор пока является bootstrap компилятором но уже собирает пользовательские программы операционной системы

## язык

iskra поддерживает

- знаковые и беззнаковые целые числа от 8 до 64 бит
- `usize` `isize` `bool` и `void`
- неизменяемые значения `let`
- изменяемые значения `var`
- константы
- обычные и внешние функции
- локальные области видимости
- `if` `else` и `while`
- арифметику сравнения битовые операции и явные casts
- структуры с выровненными полями
- константные и изменяемые указатели
- строковые литералы с escapes
- фиксированные массивы с bounds traps

полный поддерживаемый синтаксис описан в [документе языка](docs/language.md)

iskra не является memory safe языком

за корректность raw pointers отвечает программа

## компилятор

host компилятор написан на c

он состоит из следующих основных этапов

```text
исходник
lexer
parser
ast
semantic analysis
typed hir
прямой backend x86-64 assembly
```

lexer хранит имя файла строку и колонку

parser использует recursive descent для statements и pratt parser для expressions

semantic analysis проверяет имена типы mutability вызовы условия и пути return

typed hir хранит разрешённые bindings и типы выражений

backend создаёт gnu assembly а host build использует gnu assembler и ld для создания статического elf64

## сборка

```sh
make
```

компилятор создаётся по адресу `build/iskrac`

## команды

показать tokens

```sh
build/iskrac --dump-tokens examples/minimal.isk
```

показать ast

```sh
build/iskrac --dump-ast examples/minimal.isk
```

проверить программу

```sh
build/iskrac --check examples/control_flow.isk
```

показать typed hir

```sh
build/iskrac --emit-hir examples/structures.isk
```

создать assembly

```sh
build/iskrac --emit-asm examples/control_flow.isk
```

## диагностика

ошибки компилятора содержат

- имя файла
- строку
- колонку
- категорию ошибки
- короткое сообщение
- связанную строку исходника когда она доступна

стабильные категории это `lex` `parse` и `semantic`

компилятор не создаёт код после ошибки semantic analysis

## abi

целевая платформа называется `x86_64-unknown-vostok`

iskra использует целочисленную часть calling convention system v amd64

первые аргументы используют `rdi` `rsi` `rdx` `rcx` `r8` и `r9`

скалярный результат использует `rax`

стек выровнен на 16 байт перед вызовом

целевая платформа не использует red zone

обычные функции iskra имеют детерминированный symbol mangling

внешние функции сохраняют имя из исходника

подробности находятся в [документе abi](docs/abi.md)

## поддержка vostok

корневая сборка использует iskra для компиляции init shell console и других пользовательских программ

созданные программы являются статическими elf64 файлами которые загружает ядро vostok

runtime содержит assembly wrappers для ipc capabilities файлов управления процессами console и process exit

в vostok также есть меньший нативный компилятор написанный на iskra

этот компилятор работает в ring 3 и записывает `/home/a.out` прямо в ramfs

сейчас он поддерживает одну печатаемую строку и не является полным host компилятором

## тесты

```sh
make test
```

тесты проверяют lexer parser semantic checks диагностику структуры указатели строки массивы control flow арифметику и созданный machine code

корневой тест vostok также запускает программы iskra в qemu

## отсутствующие части

компилятор пока не имеет cfg ssa optimization passes machine ir register allocation прямого object output и нативного linker

язык пока не имеет modules imports generics slices динамических массивов heap ownership floating point concurrency exceptions macros и стандартной библиотеки

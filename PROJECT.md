# Kinglet

A C++-flavored systems programming language with first-class pattern matching, compiled to bytecode VM.

> Familiar to C++ developers from day one. Pattern matching as primary control flow. Runtime small enough to embed.

## Current Pipeline

```
Source → Scanner → Parser → TypeChecker → Compiler → VM
```

## Directory Structure

```
src/
├── ast/         AST node definitions
├── checker/     TypeChecker (type inference + error reporting)
├── compiler/    AST → bytecode compiler
├── kinglet/     CLI entry point (main.cc)
├── lexer/       Scanner + Token definitions
├── parser/      Recursive descent parser
├── types/       Type system (TypeKind, Type)
└── vm/          Bytecode VM (Value, Chunk, Vm)
```

## Build

```
ninja -C out/Debug
./out/Debug/kinglet [--tokens | --ast | --bytecode | --repl] [file.kl]
```

## Syntax (Implemented)

```rust
// Types
int, float, double, bool, string, void

// Variables
int x = 42;
mut x = 42;           // mutable
const x = 42;         // immutable
auto x = 42;          // type inference

// Control flow
if (condition) { ... } else { ... }
while (condition) { ... }

// Inspect (pattern matching)
mut result = inspect (x) {
    1 => 10,
    2 => 20,
    _ => 0            // wildcard
};

// Functions
int main() { ... }
int add(int a, int b) { ... }
int square(int x) => x * x;  // expression body

// I/O (Phase A — temporary)
print(42);
print(x + y);

// Operators
+ - * / % == != < > <= >= && || ! ~
= += -= *= /=
```

## Completed Phases

### P1: MVP
- [x] AST operator → enum (BinaryOp, UnaryOp, AssignOp)
- [x] Call Frame mechanism in VM
- [x] JMP/JMP_FALSE + comparison ops (Eq, Neq, Lt, Gt, Le, Ge)
- [x] if/while compilation
- [x] Inspect (pattern matching) — `inspect (x) { ... }`
- [x] REPL interactive mode (`--repl`)

### P1.5: Type Checker + Typed Literals
- [x] Typed LiteralExpr nodes (IntLiteralExpr, FloatLiteralExpr, StringLiteralExpr, BoolLiteralExpr, NullLiteralExpr)
- [x] TypeChecker layer (type inference, type error reporting)
- [x] Types library (TypeKind, Type)
- [x] Parser → TypeChecker → Compiler → VM pipeline

### P1.5+: I/O (Phase A)
- [x] NativePrint bytecode instruction
- [x] `print(expr, ...)` function
- [x] `return 0` no longer printed (exit code only)
- [x] REPL: non-zero values printed, zero suppressed

## Unfinished Plans

See `.opencode/plans/` for full detail.

### I/O System (Phase B/C)
| Task | Status | Plan |
|------|--------|------|
| `io::out()` namespace syntax | ⬜ | `.opencode/plans/io-system.md` Phase B |
| `alias io;` import mechanism | ⬜ | `.opencode/plans/io-system.md` Phase B |
| `out.line()` / `io::in()` methods | ⬜ | `.opencode/plans/io-system.md` Phase C |
| `"{}"` format strings | ⬜ | `.opencode/plans/io-system.md` Phase C |
| Cross-platform C runtime (Windows + POSIX) | ⬜ | `.opencode/plans/io-system.md` Phase C |

### Upcoming Phases

| Phase | Tasks | Status |
|-------|-------|--------|
| **P2** | struct/enum, exhaustiveness checker, full pattern matching, NaN-boxing | ⬜ |
| **P3** | trait, standard library, module system, package manager | ⬜ |
| **P4** | spawn/channel/select, work-stealing scheduler | ⬜ |

### Known Gaps
- [ ] Multi-function calls (VM CallFrame exists, compiler only handles `main()`)
- [ ] Closures / lambda expressions
- [ ] `for` loop, `break`/`continue`
- [ ] Array/Map literals
- [ ] Generics
- [ ] import/export semantics
- [ ] `dynamic_cast` usage (5 sites) — consider visitor pattern
- [ ] `Scanner::identifier_type()` rebuilds unordered_map every call — should be static

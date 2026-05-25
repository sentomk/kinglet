# TODO

## Next

- [x] Add CLI/golden tests for successful runs, diagnostics, bytecode dumps, and regressions
- [x] I/O API design: make `io::in` an input object with methods like `io::in.line(prompt)` and `io::in.secret(prompt)`
- [x] I/O API design: decide whether `io::in(prompt)` remains as shorthand for `io::in.line(prompt)`
- [x] LSP diagnostics: wider underlines, document symbols, signature help
- [x] LSP diagnostics: surface compiler warnings (unreachable code, unused vars, constant conditions)
- [x] Error message improvements (suggest `using io;` when `io::` used without it)

## P1: Syntax & Expressiveness (WG21-inspired)

- [x] Array methods: `len()`, `push()`, `pop()`, `remove()`, `contains()`, `clear()`
- [x] Chained comparisons (P0893): `1 <= x <= 10` → parser desugars to `&&`
- [x] Pipeline operator (P2011): `data |> filter |> map |> sum`
- [x] Implicit return (P0927): last expression in block is the return value
- [ ] Multi-dimensional subscript (P2169): `matrix[i, j]`
- [x] Structured unpacking (P1858): `let [a, b, ...rest] = arr;`
- [x] `guard` early-exit: `guard x > 0 else { return -1; }` — compiler enforces else must terminate
- [ ] `once` lazy init block: memoize first evaluation, zero-cost on subsequent calls
- [ ] `retry N { ... }` loop: built-in retry semantics with optional delay
- [ ] Inline tests: `test "name" { ... }` blocks, compiled out in release, run with `kinglet test`
- [ ] `scope` resource management: auto-call `.close()` on scope exit without RAII classes

## P2: Types & Patterns

- [ ] Align pattern matching syntax with WG21 P2688R5's `match` expression model
- [ ] Decide whether `inspect` remains as an alias or migrates fully to `match`
- [ ] Pattern guards using `if (...)`
- [ ] Explicit binding with `let`
- [ ] Structured binding patterns for structs, tuples, and arrays
- [ ] Exhaustiveness and usefulness checking for pattern matching
- [ ] Error propagation `?` operator (P2561): requires Result/Optional type
- [ ] Zero-overhead optional (P2723): `int? x = null;` with niche optimization
- [ ] `[[nodiscard]]` for functions (P1029): warn on unused return values

## P3: Stdlib & Toolchain

- [ ] Trait system
- [ ] Standard library
- [ ] Module system / package manager
- [ ] Closures / lambda
- [ ] Map literals

## P4: Concurrency

- [ ] Structured concurrency (P2504): `scope { spawn f(); spawn g(); }`
- [ ] spawn / channel / select
- [ ] Work-stealing scheduler

## P5: Performance & Safety

- [ ] NaN-boxing migration
- [ ] Trivial relocatability (P1144 / P2786): move as memcpy
- [ ] Lifetime safety profiles (P1179): partial borrow checking without full borrow checker
- [ ] Consider visitor pattern for `dynamic_cast` dispatch

## Done

- [x] Struct definitions
- [x] Enum definitions
- [x] Dynamic arrays: `T[]`, array literals, indexing, assignment, and bounds checks
- [x] Operators: `%` (modulo), `&&`/`||` (short-circuit), `~` (bitwise NOT)
- [x] Generics `<T>` (monomorphization: structs + functions)
- [x] TypeChecker: report unknown type names (no longer silently treated as `int`)
- [x] TypeChecker: validate struct literal field count and value types
- [x] TypeChecker: validate field assignment value type
- [x] REPL: fix stale `import io;` fallback → `using io;`
- [x] Scanner: `identifier_type()` uses a static keyword map
- [x] Multi-function support (forward references, parameters, recursion)
- [x] LSP: diagnostics, scope-aware completion, go-to-definition, hover
- [x] LSP: snippet completions (if/for/while/inspect/main/using)
- [x] LSP: `using io;` triggers `out/err/in` → `io::out` completion
- [x] LSP: type keywords prioritized in completion
- [x] LSP: keyword completions insert trailing space
- [x] LSP: document symbols (outline view), signature help (parameter hints)
- [x] LSP: wider diagnostic underlines (full token length)
- [x] LSP: io method completion (io::out. → line, io::in. → secret)
- [x] Golden tests: all modules (arithmetic, comparison, logic, structs, enums, inspect, generics, control flow, recursion, io methods)
- [x] I/O: `io::out.line`, `io::err.line`, `io::in.secret` method syntax
- [x] CI workflow with golden tests on push/PR
- [x] VSCode extension migrated to vscode-languageclient
- [x] VSCode file icon theme (light/dark)
- [x] Remove `print()` builtin (replaced by `io::out`)
- [x] Remove `import` keyword
- [x] REPL: auto-detect return type, strip trailing `;`, suppress null
- [x] `io::` requires `using io;` (not unconditional)

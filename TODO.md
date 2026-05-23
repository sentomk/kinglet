# TODO

## Next

- [ ] Add CLI/golden tests for successful runs, diagnostics, bytecode dumps, and regressions
- [ ] I/O API design: make `io::in` an input object with methods like `io::in.line(prompt)` and `io::in.secret(prompt)`
- [ ] I/O API design: decide whether `io::in(prompt)` remains as shorthand for `io::in.line(prompt)`
- [ ] Implement or remove currently accepted-but-unsupported operators: `%`, `&&`, `||`, `~`
- [ ] LSP diagnostics: surface compiler warnings (unreachable code, etc.)
- [ ] Error message improvements (suggest `using io;` when `io::` used without it)
- [ ] `using io::out;` selective import syntax

## P2: Types & Patterns

- [ ] struct definitions
- [ ] enum definitions
- [ ] Exhaustiveness checking for inspect
- [ ] Full pattern matching (destructuring, guards)
- [ ] NaN-boxing migration (optional)

## P3: Stdlib & Toolchain

- [ ] Trait system
- [ ] Standard library
- [ ] Module system / package manager

## P4: Concurrency

- [ ] spawn / channel / select
- [ ] Work-stealing scheduler

## Known Gaps

- [ ] Closures / lambda
- [ ] Array/Map literals
- [ ] Consider visitor pattern for `dynamic_cast` dispatch

## Done

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
- [x] VSCode extension migrated to vscode-languageclient
- [x] VSCode file icon theme (light/dark)
- [x] Remove `print()` builtin (replaced by `io::out`)
- [x] Remove `import` keyword
- [x] REPL: auto-detect return type, strip trailing `;`, suppress null
- [x] `io::` requires `using io;` (not unconditional)

---
tags:
  - implementation
  - core
created: 2026-05-20
---

# Architecture

## Compilation Pipeline

```
Source (.kl)
  → Lexer (tokens)
  → Parser (AST)
  → Type checker / inference
  → IR lowering
  → Bytecode compiler
  → VM execution
```

Each layer is independently testable.

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| VM type | Stack-based | Simpler than register-based; proven by Lua/Python/JVM |
| Value representation | NaN-boxing | Zero allocation for scalars; uniform 8-byte width |
| GC | Mark-and-sweep + generational | Simple and reliable; upgradeable |
| Bytecode format | Fixed endianness | Cross-platform consistency |
| Implementation language | C++17 | Embeddability requirement; consistency with patternia ecosystem |

## Module Organization

```
kinglet/
├── src/
│   ├── lexer/        → Token, Scanner
│   ├── parser/       → AST nodes, Parser
│   ├── checker/      → Type inference, exhaustiveness
│   ├── compiler/     → AST → Bytecode
│   ├── vm/           → VM, GC, value representation
│   ├── stdlib/       → Built-in functions
│   └── cli/          → REPL, build tool
├── include/
│   └── kinglet/      → Public headers (embedding API)
└── test/
```

## Related

- [[Lexer]] — lexical analysis
- [[Parser]] — syntax analysis
- [[Bytecode VM]] — virtual machine
- [[Embeddability]] — embedding API

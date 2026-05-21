---
tags:
  - design
  - core
created: 2026-05-20
---

# Philosophy

## Core Principles

| Principle | Definition |
|---|---|
| **Familiar** | C++-like syntax without common pitfalls |
| **Match-first** | Pattern matching as a first-class expression, not an auxiliary feature |
| **Portable** | Bytecode VM with unified toolchain and deterministic behavior |

## Problem Space

| Challenge | Solution |
|---|---|
| Performance | Bytecode VM with NaN-boxing; JIT-ready architecture |
| Type Safety | Gradual typing: inference by default, enforcement on annotation |
| Concurrency | spawn + channel model; no GIL; true parallelism |
| Package Management | Integrated toolchain following Cargo design principles |
| Embeddability | VM core under 50KB with clean C API |
| Startup Latency | Bytecode precompilation to `.klc` format |

## Design Constraints

When ergonomics conflict with performance, prioritize semantic simplicity to preserve optimization opportunities.

**Excluded features:**
- Implicit type conversions (source of C++ complexity)
- Runtime type modification (dynamic language performance barrier)
- Multiple inheritance (replaced by [[Traits]])
- Exceptions (replaced by [[Error Handling|Result + ?]])

## 相关

- [[Syntax]] — 具体语法设计
- [[Architecture]] — 实现架构

---
tags:
  - roadmap
  - phase-2
status: not-started
target: weeks 5-8
created: 2026-05-20
---

# Phase 2 — Types & Patterns

Complete type system and pattern matching capabilities.

## Deliverables

- [ ] `struct` definition and instantiation
- [ ] `enum` algebraic types (with data)
- [ ] Destructuring pattern matching
- [ ] Multi-clause functions
- [ ] `when` guards
- [ ] Type inference engine
- [ ] Exhaustiveness checker
- [ ] `if match` / `while match`

## Acceptance Criteria

Multi-clause `fib` function correctly computes `fib(20) == 10946`; incomplete match triggers compilation error.

## Related

- [[Phase 1 - MVP]] — prerequisite
- [[Pattern Matching]] — design document
- [[Type System]] — design document
- [[Phase 3 - Stdlib & Toolchain]] — next phase

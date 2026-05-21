---
tags:
  - roadmap
  - phase-4
status: not-started
target: weeks 13-16
created: 2026-05-20
---

# Phase 4 — Concurrency

True parallelism with spawn + channel + select.

## Deliverables

- [ ] `spawn` keyword → lightweight tasks
- [ ] Work-stealing thread pool scheduler
- [ ] `channel<T>` type-safe channels
- [ ] `select` multiplexing
- [ ] `atomic<T>` / `mutex<T>`
- [ ] [[Error Handling]]: `Result<T, E>` + `?` operator

## Acceptance Criteria

Spawn 1000 tasks, send values through channels, verify all received correctly.

## Related

- [[Phase 3 - Stdlib & Toolchain]] — prerequisite
- [[Concurrency]] — design document

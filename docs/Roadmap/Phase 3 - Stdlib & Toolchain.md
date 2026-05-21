---
tags:
  - roadmap
  - phase-3
status: not-started
target: weeks 9-12
created: 2026-05-20
---

# Phase 3 — Stdlib & Toolchain

Standard library, module system, integrated toolchain.

## Deliverables

- [ ] Standard library: string, array, map, set, I/O, file
- [ ] Module system: `import` / `export` / `namespace`
- [ ] Package manager skeleton: `kinglet.toml` dependency declaration
- [ ] CLI: `kinglet build` / `kinglet run` / `kinglet test` / `kinglet fmt`
- [ ] [[Traits]] implementation
- [ ] Bytecode precompilation `.klc`

## Acceptance Criteria

`kinglet build` + `kinglet run` work end-to-end on a multi-file project.

## Related

- [[Phase 2 - Types & Patterns]] — prerequisite
- [[Modules & Packages]] — design document
- [[Embeddability]] — C/C++ API
- [[Phase 4 - Concurrency]] — next phase

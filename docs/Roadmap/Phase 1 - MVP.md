---
tags:
  - roadmap
  - phase-1
status: not-started
target: weeks 1-4
created: 2026-05-20
---

# Phase 1 — MVP

End-to-end minimal subset: literals, variables, functions, `match`, print.

## Deliverables

- [x] [[Lexer]] — complete token set
- [x] [[Parser]] — recursive descent, AST output
- [ ] Bytecode compiler — AST → bytecode
- [ ] [[Bytecode VM]] — stack-based VM, call frames, GC stub
- [ ] REPL — interactive loop

## Supported Syntax Subset

```cpp
// Variables
int x = 42;
auto y = x + 1;

// Functions
int add(int a, int b) {
    return a + b;
}

// match
auto result = match (x) {
    0 => "zero",
    _ => "other",
};

// Control flow
if (x > 0) { println("positive"); }
while (x > 0) { x = x - 1; }

// Built-in
println("hello world");
```

## Acceptance Criteria

Current executable slice:
```cpp
int main() {
    mut count = 40;
    count += 2;
    return count;
}
```
outputs `42`.

Full MVP target:

Execute the following in REPL and output `"yes"`:
```cpp
match (42) { 0 => "no", _ => "yes" }
```

## Excluded

- struct / enum
- Type inference (except simple auto cases)
- Modules / import
- Concurrency

## Related

- [[Phase 2 - Types & Patterns]] — next phase

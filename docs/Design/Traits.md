---
tags:
  - design
  - types
created: 2026-05-20
---

# Traits

Composition over inheritance. No `class` keyword, no inheritance hierarchies. Only `struct` + `trait`.

## Definition

```cpp
trait Printable {
    string to_string();
}

trait Comparable {
    int compare(Self other);
}

trait Hashable {
    int hash();
}
```

## Implementation

```cpp
struct Point : Printable, Comparable {
    double x;
    double y;

    string to_string() => "(" + x + ", " + y + ")";
    int compare(Point other) => length(this) - length(other);
}
```

## Generic Constraints

```cpp
T max<T: Comparable>(T a, T b) => a.compare(b) > 0 ? a : b;

void print_all<T: Printable>(array<T> items) {
    for (auto item : items) {
        println(item.to_string());
    }
}
```

## Default Implementations

```cpp
trait Debug {
    string debug_string();

    // Default implementation
    void debug_print() {
        println("[DEBUG] " + debug_string());
    }
}
```

## Comparison with C++

| C++ | Kinglet |
|---|---|
| `virtual` + `override` | trait methods |
| Multiple inheritance | Multiple trait implementation |
| CRTP | `Self` type |
| concept (C++20) | trait constraint `<T: Trait>` |
| vtable overhead | Static dispatch (generics) or explicit `dyn Trait` |

## Related

- [[Type System]] — type system overview
- [[Philosophy]] — rationale for avoiding inheritance

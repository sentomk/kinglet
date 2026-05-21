---
tags:
  - design
  - types
created: 2026-05-20
---

# Type System

Gradual typing: inference by default, enforcement on annotation.

## Built-in Types

| Type | Size | Description |
|---|---|---|
| `int` | 64-bit | Signed integer |
| `float` | 64-bit | IEEE 754 double |
| `bool` | 1 byte | `true` / `false` |
| `string` | Variable | UTF-8, immutable |
| `byte` | 1 byte | Unsigned 8-bit |
| `void` | 0 | No return value |

## Compound Types

| Type | Syntax | Description |
|---|---|---|
| Array | `array<T>` | Dynamic array |
| Map | `map<K, V>` | Hash table |
| Set | `set<T>` | Hash set |
| Optional | `optional<T>` | Some(T) \| None |

These are first-class types, not library constructs.

## Type Inference

```cpp
auto x = 42;         // int
auto y = 3.14;        // float
auto s = "hello";     // string
auto nums = [1,2,3];  // array<int>
```

## Explicit Annotation

```cpp
int x = 42;
string name = "hello";
array<float> coords = [1.0, 2.0, 3.0];
```

Annotations enforce compile-time type checking. Type mismatches produce errors.

## Function Generics

```cpp
// auto parameters = implicit generics
auto add(auto a, auto b) => a + b;

// Explicit generics
T max<T>(T a, T b) => a > b ? a : b;

// Constrained generics (with Traits)
T max<T: Comparable>(T a, T b) => a.compare(b) > 0 ? a : b;
```

## User-Defined Types

### struct

```cpp
struct Point {
    double x;
    double y;
}
```

### enum (algebraic types / tagged unions)

```cpp
enum Shape {
    Circle(double),
    Rect(double, double),
    Triangle(Point, Point, Point),
}

enum Option<T> {
    Some(T),
    None,
}
```

Enums carry data and integrate with [[Pattern Matching]] for destructuring.

## Cross-Platform Consistency

- `int` is always 64-bit, platform-independent
- `float` is always IEEE 754 double
- `string` is always UTF-8
- No platform-dependent types (`size_t`, `long`, `short`)

## Related

- [[Traits]] — type constraints and polymorphism
- [[Pattern Matching]] — destructuring and exhaustiveness
- [[Error Handling]] — Result<T, E> type

---
tags:
  - design
  - core
  - pattern-matching
created: 2026-05-20
---

# Pattern Matching

Pattern matching in Kinglet provides the capabilities that patternia approximates in C++ through macro-based DSLs. Native language support eliminates the need for `PTN_ON`, `PTN_WHERE`, and `PTN_LET` workarounds.

## match Expression

`match` is an expression that returns a value.

```cpp
auto describe(int n) -> string {
    return match (n) {
        0 => "zero",
        1 | 2 | 3 => "small",
        n when n > 100 => "big: " + to_string(n),
        _ => "medium",
    };
}
```

### Syntax Elements

| Element | Syntax | Description |
|---|---|---|
| Literal | `0`, `"hello"`, `true` | Exact match |
| Wildcard | `_` | Match any value without binding |
| Variable binding | `n` | Match any value and bind to `n` |
| Or pattern | `1 \| 2 \| 3` | Match any alternative |
| Guard | `n when n > 0` | Additional condition |
| Destructuring | `Point{x, y}` | Extract struct fields |
| Nested | `BinOp(Add, Literal(a), Literal(b))` | Recursive destructuring |

### Exhaustiveness Checking

The compiler enforces exhaustiveness for `enum` types. Missing cases result in compilation errors.

```cpp
enum Color { Red, Green, Blue }

// Compilation error: Blue not covered
match (c) {
    Red => "red",
    Green => "green",
}
```

## Destructuring Patterns

```cpp
struct Point { double x; double y; };

enum Shape {
    Circle(double),
    Rect(double, double),
    Triangle(Point, Point, Point),
}

double area(Shape s) {
    return match (s) {
        Circle(r) => math.pi * r * r,
        Rect(w, h) => w * h,
        Triangle(a, b, c) => heron(a, b, c),
    };
}
```

## Multi-Clause Functions

Function definitions as pattern matching, following Erlang/Elixir conventions.

```cpp
int fib(0) => 1;
int fib(1) => 1;
int fib(int n) => fib(n - 1) + fib(n - 2);

string classify(int n) when n < 0  => "negative";
string classify(0)                  => "zero";
string classify(int n) when n > 0  => "positive";

// Parameter destructuring
double length(Point{x, y}) => math.sqrt(x*x + y*y);
```

The compiler desugars multi-clause functions into a single function with an internal `match`. Exhaustiveness checking spans all clauses.

## if match / while match

Similar to Rust's `if let` / `while let`:

```cpp
if match (find(map, "key")) Some(v) {
    println("found: ", v);
}

while match (stream.next()) Some(line) {
    process(line);
}
```

## for Destructuring

```cpp
for (auto [key, value] : table) {
    println(key, " => ", value);
}
```

## Nested Patterns

Optimal for compiler pass implementation:

```cpp
match (expr) {
    BinOp(Add, Literal(a), Literal(b)) => Literal(a + b),
    BinOp(Mul, x, Literal(0))          => Literal(0),
    BinOp(Mul, x, Literal(1))          => x,
    node                               => node,
}
```

Complete example: [[Constant Folding]].

## Comparison with patternia

| Capability | patternia (C++ DSL) | Kinglet (native) |
|---|---|---|
| Caching | `PTN_ON` macro / `static_on` | Compiler optimization |
| Guards | `PTN_WHERE((x), x > 0)` macro | `when x > 0` keyword |
| Single-name guards | `PTN_LET(x, x > 0)` | `when` covers all cases |
| Exhaustiveness | None (runtime `_` fallback) | Compile-time enforcement |
| Nested destructuring | Manual recursive visitor | Direct syntax support |
| Multi-clause functions | Not supported | Native support |

## Related

- [[Syntax]] — base syntax
- [[Type System]] — enum algebraic types
- [[Constant Folding]] — practical example

---
tags:
  - design
  - syntax
created: 2026-05-20
---

# Syntax

C++ subset: retain familiar constructs, eliminate complexity.

## Retained Features

`{}` blocks, `;` terminators, `//` comments, type-before-name declarations, `for`/`while`/`if`, `struct`/`enum`, namespaces, `auto`

## Removed Features

`#include`/preprocessor, header files, templates (replaced by simple generics), multiple inheritance, implicit conversions, raw pointers, `operator` overloading (except whitelist), `friend`, `virtual`/`override` (replaced by [[Traits]])

## Variables and Constants

```cpp
int x = 42;
auto y = 3.14;           // f64 inferred
string name = "hello";
bool flag = true;
const int MAX = 1024;
```

## Compound Literals

```cpp
auto nums = [1, 2, 3];           // array<int>
auto table = {"a": 1, "b": 2};  // map<string, int>
```

## Functions

```cpp
// Standard form
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// Expression body (single expression, braces omitted)
double circle_area(double r) => math.pi * r * r;

// Generics
T max<T>(T a, T b) => a > b ? a : b;

// Closures — C++ capture syntax retained
auto add = [](int a, int b) => a + b;
auto counter = [n = 0]() mutable => ++n;
```

## Related

- [[Pattern Matching]] — match expressions and multi-clause functions
- [[Type System]] — gradual typing
- [[Lexer]] — token set

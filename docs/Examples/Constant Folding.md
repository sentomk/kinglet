---
tags:
  - example
  - compiler
created: 2026-05-20
---

# Constant Folding

Compiler optimization pass written in Kinglet—demonstrates multi-clause functions and nested pattern matching.

```cpp
import std.io;

enum Expr {
    Literal(int),
    BinOp(Op, Expr*, Expr*),
    UnaryOp(Op, Expr*),
    Var(string),
}

enum Op { Add, Sub, Mul, Div, Neg }

// Constant folding — each clause is an optimization rule
Expr fold(Literal(n)) => Literal(n);

Expr fold(BinOp(Add, Literal(a), Literal(b))) => Literal(a + b);
Expr fold(BinOp(Sub, Literal(a), Literal(b))) => Literal(a - b);
Expr fold(BinOp(Mul, Literal(a), Literal(b))) => Literal(a * b);
Expr fold(BinOp(Div, Literal(a), Literal(b))) when b != 0 => Literal(a / b);

// Algebraic simplification
Expr fold(BinOp(Mul, _, Literal(0))) => Literal(0);
Expr fold(BinOp(Mul, Literal(0), _)) => Literal(0);
Expr fold(BinOp(Mul, x, Literal(1))) => fold(x);
Expr fold(BinOp(Mul, Literal(1), x)) => fold(x);
Expr fold(BinOp(Add, x, Literal(0))) => fold(x);
Expr fold(BinOp(Add, Literal(0), x)) => fold(x);

// Recursive folding
Expr fold(BinOp(op, left, right)) => BinOp(op, fold(left), fold(right));
Expr fold(UnaryOp(Neg, Literal(n))) => Literal(-n);
Expr fold(other) => other;

void main() {
    // 3 + (4 * 1) → fold → Literal(7)
    auto expr = BinOp(Add, Literal(3), BinOp(Mul, Literal(4), Literal(1)));
    auto result = fold(expr);
    println(result);  // Literal(7)
}
```

## Comparison

In C++, this requires visitor pattern with substantial boilerplate. In patternia, it requires `PTN_ON` and `PTN_WHERE` macros. In Kinglet, each line corresponds directly to a derivation rule from a textbook.

## Related

- [[Pattern Matching]] — syntax details
- [[Pattern Matching#Multi-Clause Functions]] — multi-clause functions

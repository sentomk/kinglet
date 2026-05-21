---
tags:
  - implementation
  - phase-1
created: 2026-05-20
---

# Parser

Recursive descent parser — token stream → AST.

## Rationale for Recursive Descent

- Hand-written, full control over error messages
- Natural support for Pratt parsing (operator precedence)
- No external tools required (yacc/bison)
- Standard choice for modern language compilers (Clang, Rust, Go)

## AST Nodes (Phase 1 Subset)

```cpp
// Expressions
enum Expr {
    Literal(Value),
    Identifier(string),
    Binary(Op, Expr*, Expr*),
    Assign(string name, Op, Expr* value),
    Unary(Op, Expr*),
    Call(Expr*, array<Expr*>),
    Match(Expr*, array<MatchArm>),
    Block(array<Stmt*>),
}

// Statements
enum Stmt {
    VarDecl(string name, optional<Type> type, Expr* init),
    ExprStmt(Expr*),
    Return(optional<Expr*>),
    FnDecl(string name, array<Param> params, Type ret, Expr* body),
    If(Expr* cond, Stmt* then, optional<Stmt*> else_),
    While(Expr* cond, Stmt* body),
    For(Pattern pat, Expr* iter, Stmt* body),
}

// match arm
struct MatchArm {
    Pattern pattern;
    optional<Expr*> guard;   // when guard
    Expr* body;
}
```

## Precedence Table (Pratt Parsing)

| Precedence | Operators | Associativity |
|---|---|---|
| 1 | `\|\|` | Left |
| 2 | `&&` | Left |
| 3 | `== != < > <= >=` | Left |
| 4 | `\| ^` | Left |
| 5 | `& << >>` | Left |
| 6 | `+ -` | Left |
| 7 | `* / %` | Left |
| 8 | `! - ~` (prefix) | Right |
| 9 | `.` `()` `[]` (postfix) | Left |

## Error Recovery

- Synchronize to next statement boundary on error (`;`, `}`, keyword)
- Report as many errors as possible, do not stop at first error
- Error messages include line number, column, and context

## Related

- [[Lexer]] — upstream input
- [[Pattern Matching]] — match expression parsing
- [[Phase 1 - MVP]] — implementation phase

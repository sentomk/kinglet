---
tags:
  - review
  - architecture
created: 2026-05-21
---

# Architecture Review

Comprehensive review of Kinglet compiler architecture as of 2026-05-21.

## 一、Overall Assessment

The project has a clean structure with well-organized documentation. The four-layer separation (Lexer → Parser → Compiler → VM) is sound, and a working end-to-end slice has been validated. The codebase demonstrates solid engineering discipline with error recovery, multi-mode CLI debugging, and consistent naming.

## 二、Compilation Pipeline Gap

**Current**: `Source → Lexer → Parser → Compiler → VM`
**Designed**: `Source → Lexer → Parser → Type Checker → IR Lowering → Bytecode Compiler → VM`

| Missing Layer | Impact | Priority |
|---|---|---|
| **Type Checker** | No type inference, exhaustiveness checking, generic constraints | High |
| **IR** | Direct AST → bytecode leaves no optimization layer | Medium |

**Recommendation**: IR can wait until Phase 2+, but the Type Checker interface should be defined during MVP to avoid AST refactoring later.

## 三、Critical Design Issues

### 3.1 AST Operator Representation

**Problem**: `BinaryExpr`, `UnaryExpr`, `AssignExpr` use `std::string op` for operators.

```cpp
// Current
struct BinaryExpr { std::string op; };  // op = "+", "-", "*"

// Recommended
enum class BinaryOp { Add, Sub, Mul, Div, Eq, Neq, Lt, Gt, Le, Ge, And, Or, ... };
struct BinaryExpr { BinaryOp op; };
```

**Impact**:
- Compiler uses string comparison (`if (binary->op == "+")`) — inefficient and error-prone
- No compile-time operator validation
- Adding operators requires changes across multiple string-matching sites

**Recommendation**: Refactor early. Cost grows with each new feature that depends on operators.

### 3.2 Value Representation vs NaN-boxing Design

**Designed**: NaN-boxing, all values in unified 8 bytes
**Current**: Tagged union with `type` enum + 3 separate fields (~24 bytes)

```cpp
// Current: ~24 bytes
struct Value {
    ValueType type;
    int64_t int_value_storage;
    double double_value_storage;
    bool bool_value_storage;
};
```

**Impact**: 3× memory overhead vs NaN-boxing; less efficient GC scanning.

**Recommendation**: Keep current design for MVP (lower complexity). Migrate to NaN-boxing in Phase 2 when heap objects (string, array) are introduced.

### 3.3 VM Missing Call Frame Mechanism

**Current**: Single `stack_` + `locals_`, no function call support
**Designed**: `CallFrame { function*, ip, slots* }` stack

**Impact**: Blocks Phase 1 MVP — cannot:
- Call functions (including `main()` calling other functions)
- Support recursion
- Implement closures

**Recommendation**: Implement Call Frame as the next highest priority after AST operator refactoring.

### 3.4 Control Flow Missing

**Current**: Compiler does not handle `if`/`while`; VM has no jump instructions
**Needed**: `JMP`, `JMP_FALSE`, comparison operators (`EQ`, `NEQ`, `LT`, `GT`, `LE`, `GE`)

### 3.5 Parser Missing Key Syntax

| Syntax | Doc Requirement | Status |
|---|---|---|
| `match` expression | Phase 1 MVP core | Not implemented |
| Array/Map literals | `[1, 2, 3]`, `{"a": 1}` | Not implemented |
| Closures | `[](int a) => a + b` | Not implemented |
| `for` loop | Basic control flow | Not implemented |
| `break`/`continue` | Loop control | Token exists, Parser ignores |

## 四、Design Consistency Check

| Feature | Documented | Implemented | Status |
|---|---|---|---|
| `const`/`mut` storage | Yes | Yes (Compiler checks mutability) | ✅ |
| Expression body `=>` | Yes | Yes (Parser → ReturnStmt) | ✅ |
| Generics `T max<T>(T, T)` | Yes | No | ❌ |
| `import`/`export` | Yes | `ImportDecl` exists, no semantics | ⚠️ |
| `struct`/`enum` | Phase 2 | No | On track |
| `trait` | Phase 3 | No | On track |
| `spawn`/`channel` | Phase 4 | No | On track |

## 五、Code Quality Observations

### Strengths
- Lexer is complete: hex/binary/numeric separators all supported
- Parser has robust error recovery (`synchronize()`)
- Compiler correctly manages local variable scoping
- CLI supports multi-mode debugging (`--tokens`, `--ast`, `--bytecode`)

### Improvement Areas
- `dynamic_cast` used 5 times in Compiler — consider visitor pattern or tagged union
- `LiteralExpr::value` stores all literals as `string`, requiring re-parsing in Compiler
- `FunctionDecl` only compiles functions named `main`, no multi-function support
- `Scanner::identifier_type()` rebuilds `unordered_map` on every call — should be `static const` or perfect hash

## 六、Architecture Evolution Plan

### Phase 1 (Now → MVP)

| # | Task | Rationale |
|---|---|---|
| 1 | AST operator → enum | Low effort, high ROI, blocks nothing |
| 2 | Add Call Frame to VM | Required for function calls |
| 3 | Add JMP/JMP_FALSE + comparison ops | Required for if/while |
| 4 | Implement `match` expression | Core MVP feature (Parser + Compiler + VM) |
| 5 | Compile `if`/`while` | Basic control flow |
| 6 | REPL | Interactive development |

### Phase 1.5 (Post-MVP)

| # | Task | Rationale |
|---|---|---|
| 7 | Add Type Checker layer (minimal: inference + errors) | Foundation for Phase 2 |
| 8 | Typed LiteralExpr nodes (IntLiteral, FloatLiteral, StringLiteral...) | Eliminate string re-parsing |

### Phase 2

| # | Task | Rationale |
|---|---|---|
| 9 | struct/enum AST + type checking | Algebraic data types |
| 10 | Exhaustiveness checker | Match safety |
| 11 | Full pattern matching (destructuring, guards, multi-clause) | Language differentiator |
| 12 | NaN-boxing migration (optional) | Memory efficiency |

## 七、Open Decisions

1. **AST operator enum** — Refactor now or later? Earlier is cheaper.
2. **IR layer** — Needed for MVP, or defer to Phase 2?
3. **NaN-boxing** — Keep tagged union for MVP, or implement NaN-boxing from the start?
4. **match expression VM design** — Core differentiator; instruction set needs careful design.

## 八、Current Working Slice

The following compiles and runs end-to-end:

```cpp
int main() {
    mut count = 40;
    count += 2;
    return count;
}
```

Output: `42`

This validates the pipeline: `Scanner → Parser → Compiler → VM` works for arithmetic, local variables, compound assignment, and return.

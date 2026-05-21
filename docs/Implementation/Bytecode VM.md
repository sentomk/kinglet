---
tags:
  - implementation
  - phase-1
created: 2026-05-20
---

# Bytecode VM

Stack-based virtual machine.

Current implementation status: the executable slice supports compiling and running
`main()` with literal values, unary `-` / `!`, arithmetic `+ - * /`, local variables,
assignment (`= += -= *= /=`), and `return`.
NaN-boxing, call frames, globals, block-scoped locals, and GC remain design targets for later slices.

## Value Representation — NaN-boxing

Type information encoded in the NaN payload of IEEE 754 doubles:

```
Normal double:  Direct storage (all non-NaN values)
NaN region:     Utilize quiet NaN's 51-bit payload for other types

[S][EEEEEEEEEEE][1][TTT][PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP]
 1     11 bits    1  3b                    48 bits
sign   exponent  QNaN type                payload
```

| Type Tag | Description |
|---|---|
| `000` | Pointer (objects: string, array, struct, etc.) |
| `001` | int (48-bit range inlined) |
| `010` | bool |
| `011` | null |

Single `uint64_t` represents all values with zero boxing overhead.

## Instruction Set (Phase 1 Core)

Implemented first slice:

| Instruction | Operation |
|---|---|
| `Constant` | Push constant |
| `Null/True/False` | Push singleton literal |
| `Add/Subtract/Multiply/Divide` | Arithmetic |
| `Negate/Not` | Unary operations |
| `LoadLocal/StoreLocal` | Function-local variable access |
| `Pop` | Drop stack top |
| `Return` | Return stack top |

Planned Phase 1 core:

| Instruction | Operation |
|---|---|
| `OP_CONST` | Push constant |
| `OP_ADD/SUB/MUL/DIV` | Arithmetic |
| `OP_EQ/NEQ/LT/GT/LE/GE` | Comparison |
| `OP_NOT/NEG` | Unary operations |
| `OP_LOAD/STORE` | Local variable read/write |
| `OP_GLOAD/GSTORE` | Global variable read/write |
| `OP_JMP/JMP_FALSE` | Unconditional/conditional jump |
| `OP_CALL/RET` | Function call/return |
| `OP_MATCH` | Pattern matching dispatch |
| `OP_PRINT` | Built-in output (temporary) |
| `OP_POP` | Pop stack top |
| `OP_HALT` | Halt execution |

## Call Frames

```cpp
struct CallFrame {
    Function* function;
    uint8_t* ip;        // Instruction pointer
    Value* slots;       // Stack frame base
};
```

VM maintains a `CallFrame` stack. Each `OP_CALL` pushes a new frame; `OP_RET` pops.

## Garbage Collection

Phase 1: Simple mark-and-sweep.
- Mark from root set (globals + stack + call frames)
- Sweep unmarked objects
- Triggered by allocation pressure (threshold doubling strategy)

Phase 3+: Upgrade to generational GC.

## Bytecode Format

```
Magic: "KLC\0"     (4 bytes)
Version: u16       (format version)
Constants: [...]   (constant pool)
Functions: [...]   (function table)
Bytecode: [...]    (instruction stream)
```

Fixed little-endian. Fully cross-platform consistent.

## Related

- [[Architecture]] — overall architecture
- [[Embeddability]] — embedding API
- [[Phase 1 - MVP]] — implementation phase

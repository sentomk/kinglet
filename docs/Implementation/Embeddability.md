---
tags:
  - implementation
  - phase-3
created: 2026-05-20
---

# Embeddability

Target: Lua-level embedding experience. VM core under 50KB.

## C API

```cpp
#include <kinglet/vm.h>

// Creation & destruction
kl_State* kl_new();
void kl_free(kl_State* state);

// Load & execute
int kl_load_file(kl_State* state, const char* path);
int kl_load_string(kl_State* state, const char* source);
int kl_run(kl_State* state);

// Call script functions
int kl_call(kl_State* state, const char* fn, int nargs);

// Stack operations
void kl_push_int(kl_State* state, int64_t val);
void kl_push_float(kl_State* state, double val);
void kl_push_string(kl_State* state, const char* str);
void kl_push_bool(kl_State* state, bool val);
int64_t kl_to_int(kl_State* state, int index);
double kl_to_float(kl_State* state, int index);
const char* kl_to_string(kl_State* state, int index);

// Register C functions
void kl_register(kl_State* state, const char* name, kl_CFunction fn);
```

## C++ Wrapper

```cpp
#include <kinglet/kinglet.hpp>

kinglet::VM vm;
vm.load_file("game_logic.kl");
vm.call("on_update", delta_time);
auto score = vm.get<int>("player_score");

// Register C++ functions
vm.bind("spawn_enemy", [&](double x, double y) {
    game.spawn_enemy(x, y);
});
```

## Design Principles

- Minimal dependencies: C standard library only
- Single-header option: provide `kinglet.h` amalgamation
- Thread-safe: each `kl_State` independent, no global state
- No exceptions: C API returns error codes

## Comparison

| | Lua | Python | Kinglet |
|---|---|---|---|
| Embedding size | ~30KB | ~5MB | <50KB target |
| C API style | Stack-based | Reference counting | Stack-based (Lua-like) |
| No global state | Yes | No (GIL) | Yes |
| Type safety | None | None | Optional |

## Related

- [[Bytecode VM]] — runtime
- [[Architecture]] — module organization
- [[Phase 3 - Stdlib & Toolchain]] — implementation phase

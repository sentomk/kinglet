---
tags:
  - design
  - toolchain
created: 2026-05-20
---

# Modules & Packages

File as module, directory as package. No header files.

## Modules

```cpp
// src/math/vector.kl
namespace math;

export struct Vec3 { double x; double y; double z; };
export double dot(Vec3 a, Vec3 b) => a.x*b.x + a.y*b.y + a.z*b.z;
```

- `export` marks public symbols
- Unmarked symbols are module-private
- One file = one module

## Imports

```cpp
import math.vector;            // Import entire module
import math.vector.{Vec3, dot}; // Selective import

auto v = Vec3{1.0, 2.0, 3.0};
```

## Integrated Toolchain

All tools integrated into a single binary:

```bash
kinglet init myproject    # Create project
kinglet add json@1.2      # Add dependency
kinglet build             # Compile
kinglet test              # Test
kinglet run               # Run
kinglet fmt               # Format
```

## Project Structure

```
myproject/
├── kinglet.toml      # Project configuration & dependencies
├── src/
│   ├── main.kl       # Entry point
│   └── lib/
│       └── utils.kl
└── test/
    └── utils_test.kl
```

## File Extension

`.kl` — concise, corresponds to "kinglet".

## Related

- [[Phase 3 - Stdlib & Toolchain]] — implementation plan
- [[Philosophy]] — unified toolchain rationale

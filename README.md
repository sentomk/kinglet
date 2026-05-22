# Kinglet

A C++-flavored systems programming language with first-class pattern matching, compiled to bytecode VM.

> Familiar to C++ developers from day one. Pattern matching as primary control flow. Runtime small enough to embed.

## Build

```bash
ninja -C out/Debug
./out/Debug/kinglet [--tokens | --ast | --bytecode | --repl] <file.kl>
```

## Quick Example

```rust
using namespace io;

int main() {
    for (int i = 0; i < 5; i += 1) {
        io::out("{}\n", i);
    }

    auto result = inspect (2) {
        1 => "one",
        2 => "two",
        _ => "other"
    };
    io::out("{}\n", result);

    return 0;
}
```

## Syntax

```rust
// Types
int x = 42;          mut x = 42;          const x = 42;

// Control flow
if (x > 0) { ... } else { ... }
while (x < 10) { ... }
for (int i = 0; i < 10; i += 1) { ... }

// Pattern matching
inspect (x) { 1 => a, 2 => b, _ => c }

// I/O
using io;           io::out("{}", x);
using namespace io; out("hello\n");

// Functions
int add(int a, int b) => a + b;
```

## Operators

`+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `&&` `||` `!` `~` `=` `+=` `-=` `*=` `/=`

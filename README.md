<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/brand/brand-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/brand/brand.svg">
    <img src="assets/brand/brand.svg" width="420" alt="Kinglet">
  </picture>
</p>

<p align="center">A bytecode-compiled language exploring the C++ proposals that deserved a second life.</p>

<p align="center">
  <a href="https://github.com/sentomk/kinglet/releases"><img src="https://img.shields.io/github/v/tag/sentomk/kinglet?label=version&sort=semver" alt="Version"></a>
  <a href="https://github.com/sentomk/kinglet/actions"><img src="https://img.shields.io/github/actions/workflow/status/sentomk/kinglet/ci.yml?branch=main" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/sentomk/kinglet" alt="License"></a>
</p>

> [!NOTE]
> Familiar semantics. Curated ideas from WG21 proposals that were deferred or rejected. Kinglet is proposal-inspired, not proposal-compatible: it adapts syntax and semantics when that makes the language smaller, clearer, or more coherent.

## Install

Download the latest release for your platform from [Releases](https://github.com/sentomk/kinglet/releases):

| Platform | Archive |
|----------|---------|
| Windows x64 | `kinglet-windows-x64.tar.gz` |
| Linux x64 | `kinglet-linux-x64.tar.gz` |
| macOS ARM64 | `kinglet-macos-arm64.tar.gz` |

Extract and add the directory to your `PATH`:

```bash
tar xzf kinglet-<platform>.tar.gz
# Move kinglet and kinglet-lsp to a directory on your PATH
```

### VSCode Extension

Install the `.vsix` from the same release page:

```bash
code --install-extension kinglet-0.0.2.vsix
```

Or build from source — see below.

## Build

```bash
gn gen out/Release --args='is_debug=false'
ninja -C out/Release
./out/Release/kinglet [--tokens | --ast | --bytecode | --repl] <file.kl>
```

## Quick Example

```cpp
using io;

struct Point {
  int x;
  int y;
}

struct Box<T> {
  T value;
}

T identity<T>(T x) => x;

int distance_sq(Point a, Point b) {
  int dx = a.x - b.x;
  int dy = a.y - b.y;
  return dx * dx + dy * dy;
}

int main() {
  Point origin { 0, 0 };
  Point target { 3, 4 };
  io::out("{}\n", distance_sq(origin, target)); // 25

  Box<int> bi { 42 };
  Box<string> bs { "hello" };
  io::out("{}\n", bi.value);            // 42
  io::out("{}\n", identity<string>("world")); // world

  // Mutation
  target.x = 6;
  target.y = 8;
  io::out("{}\n", distance_sq(origin, target)); // 100

  return 0;
}
```

## Syntax

```cpp
// Types
int x = 42;
double pi = 3.14;
string name = "kinglet";
bool flag = true;

// Structs & Enums
struct Vec2 { int x; int y; }
enum Color { Red, Green, Blue, }
Vec2 v { 1, 2 };
Color c = Color::Red;

// Generics (monomorphized)
struct Pair<A, B> { A first; B second; }
T identity<T>(T x) => x;
Pair<int, string> p { 1, "one" };
int n = identity<int>(42);

// Control flow
if x > 0 { ... } else { ... }
while count > 0 { ... }
for (int i = 0; i < 10; i = i + 1) { ... }

// Pattern matching (currently literal patterns plus wildcard)
string r = inspect value {
  0 => "zero",
  1 => "one",
  _ => "other",
};

// Planned: structural matching with guard-only arms and reflected fields.
// Planned shape:
// string q = inspect point {
//   Point(.x == .y) => "diagonal",
//   Point(.x == 0) => "on y-axis",
//   Point(.y == 0) => "on x-axis",
//   $[point.x > 0 && point.y > 0] => "quadrant I",
//   _ => "somewhere else",
// };

// I/O
using io;
io::out("{} + {} = {}\n", 1, 2, 3);
string line = io::in("prompt> ");

// Functions
int add(int a, int b) => a + b;
int factorial(int n) {
  if n <= 1 { return 1; }
  return n * factorial(n - 1);
}
```

## Operators

`+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `&&` `||` `!` `~`

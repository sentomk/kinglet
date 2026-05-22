---
tags:
  - plan
  - language-design
  - features
created: 2026-05-22
---

# Kinglet 语言特性设计文档

> 基于 C++ 委员会废弃/拒绝/搁置的提案，结合 Kinglet 自身定位，
> 定义每个特性的语法、语义、类型规则和实现要点。

---

## 设计原则

```
1. C++ 开发者从第一天就熟悉 — 语法不发明新花样
2. 模式匹配是一级控制流 — 不是 switch 的语法糖
3. 所有不安全的行为必须显式标注 — 没有隐式 UB
4. 值语义默认，引用显式 — 和 C++ 一样
5. 编译时能检查的绝不留到运行时
```

---

## P2：核心类型系统与控制流

### 2.1 enum（代数数据类型 ADT）

**C++ 对应：** enum class 关联数据的增强提案 — 多次被拒
**Kinglet 理由：** 没有 ADT 就没有真正的 pattern matching，这是语言的核心

#### 语法

```kinglet
// 简单枚举（类似 C++ enum class）
enum Color {
    Red,
    Green,
    Blue,
}

// 关联数据的枚举（C++ 做不到）
enum Shape {
    Circle(float radius),
    Rectangle(float width, float height),
    Triangle(float a, float b, float c),
}

// 关联多个命名字段
enum Expr {
    Literal(int value),
    Add(Expr left, Expr right),
    Mul(Expr left, Expr right),
    Neg(Expr inner),
}

// 泛型枚举
enum Option<T> {
    Some(T),
    None,
}

enum Result<T, E> {
    Ok(T),
    Err(E),
}
```

#### 语义

```
- enum 成员叫 variant（变体）
- 每个 variant 可以关联零个或多个字段
- variant 关联的字段用位置访问，也可以命名
- enum 默认不可比较（没有 ==），除非所有字段都可比较
- enum 默认不可拷贝，如果字段不可拷贝
- 不存在隐式转换到 int
```

#### 类型规则

```
Color c = Color::Red;                   // OK
Shape s = Shape::Circle(3.14);          // OK，构造 Circle
Shape s2 = Shape::Rectangle(1.0, 2.0); // OK，构造 Rectangle
int x = Color::Red;                     // 错误，不能隐式转 int
Color c = 0;                            // 错误，不能从 int 隐式转
```

#### 内存布局

```
enum Shape {
    Circle(float radius),
    Rectangle(float width, float height),
}

内存布局：
  [tag: 1 byte] [padding: 3 bytes] [payload: max(sizeof(Circle), sizeof(Rectangle))]

  tag = 0 → Circle, payload = float radius
  tag = 1 → Rectangle, payload = { float width, float height }

  sizeof(Shape) = 4 (tag+padding) + 8 (max payload) = 12 bytes
```

#### 编译器实现要点

```
1. AST 新增 EnumDecl 节点
   - name: string
   - variants: vector<EnumVariant>
   - EnumVariant { name, fields: vector<Type> }

2. TypeChecker
   - 注册 enum 类型
   - variant 构造时检查字段数量和类型
   - 禁止隐式 int 转换

3. Compiler
   - enum 值 = tag + payload
   - variant 构造 = 写 tag + 写各字段
   - 存入 Chunk 常量池或运行时构造

4. VM
   - Value 新增 EnumValue 类型
     enum class ValueType {
         Int, Double, Bool, Null, String, Enum,
     };
   - EnumValue { enum_type_id, variant_index, vector<Value> fields }
```

---

### 2.2 struct + 解构

**C++ 对应：** structured bindings (C++17) 但受限 — 函数参数中不能用、不能自定义规则
**Kinglet 理由：** struct 是值类型的基础，解构让 pattern matching 有意义

#### 语法

```kinglet
// 声明
struct Point {
    float x;
    float y;
}

// 构造
Point p = Point(1.0, 2.0);            // 位置构造
Point p = Point(x: 1.0, y: 2.0);     // 命名构造

// 解构
auto [px, py] = p;                    // 位置解构
auto Point { x, y } = p;             // 命名解构

// 解构进已声明变量
auto Point { x, y } = get_point();
io::out("{}", x);

// struct 嵌套
struct Line {
    Point start;
    Point end;
}

Line l = Line(Point(0, 0), Point(1, 1));
auto Line { Point { x: x1, y: y1 }, Point { x: x2, y: y2 } } = l;
```

#### 语义

```
- struct 是值类型，默认栈分配
- 拷贝是深拷贝（和 C++ 一样）
- 字段默认不可变，除非 struct 实例声明为 mut
- struct 没有 vtable，没有继承
- 解构是编译时操作，零运行时开销
```

#### 类型规则

```
Point p = Point(1.0, 2.0);           // OK
p.x = 3.0;                           // 错误，p 不是 mut
mut Point p2 = Point(1.0, 2.0);
p2.x = 3.0;                          // OK

// 命名构造时字段顺序无关
Point p = Point(y: 2.0, x: 1.0);    // OK，等价于 Point(1.0, 2.0)

// 缺字段
Point p = Point(x: 1.0);            // 错误，缺少 y
```

#### @derive（P3 预告）

```
// P3 会支持自动派生
@derive(Eq, Format)
struct Point {
    float x;
    float y;
}
// 编译器自动生成 == 运算符和 to_string() 方法
```

#### 编译器实现要点

```
1. AST 新增 StructDecl 节点
   - name: string
   - fields: vector<StructField>
   - StructField { name, type, offset }

2. TypeChecker
   - 计算 struct 布局和大小（含 alignment padding）
   - 构造时检查字段数量、类型
   - 解构时检查模式匹配

3. Compiler
   - struct 值 = 连续内存块
   - 字段访问 = 偏移量读写
   - 解构 = 编译时展开为逐字段赋值

4. VM
   - Value 新增 StructValue
   - StructValue { type_id, vector<Value> fields }
   - 字段访问通过索引（编译时已解析）
```

---

### 2.3 inspect 增强（模式匹配）

**C++ 对应：** P2688 — 搁置 5 年以上，关键字都有争议
**Kinglet 理由：** 已经有基础版 inspect，现在是核心增强

#### 语法

```kinglet
// 已有：字面量 + 通配符
mut result = inspect (x) {
    1 => "one",
    2 => "two",
    _ => "other",
};

// 新增：enum 解构
mut area = inspect (shape) {
    Shape::Circle(r) => 3.14159 * r * r,
    Shape::Rectangle(w, h) => w * h,
    _ => 0,
};

// 新增：struct 解构
auto [px, py] = inspect (point) {
    Point { x, y } => [x, y],
};

// 新增：guard 条件
mut desc = inspect (shape) {
    Shape::Circle(r) if r > 10.0 => "big circle",
    Shape::Circle(r) => "small circle",
    Shape::Rectangle(w, h) if w == h => "square",
    Shape::Rectangle(w, h) => "rectangle",
};

// 新增：嵌套解构
mut result = inspect (expr) {
    Expr::Literal(v) => v,
    Expr::Add(Expr::Literal(a), Expr::Literal(b)) => a + b,
    Expr::Add(left, right) => eval(left) + eval(right),
    Expr::Neg(Expr::Literal(v)) => -v,
    Expr::Neg(inner) => -eval(inner),
};

// 新增：范围模式
mut category = inspect (age) {
    0..12 => "child",
    13..17 => "teenager",
    18..64 => "adult",
    _ => "senior",
};

// 新增：or 模式
mut is_primary = inspect (color) {
    Color::Red | Color::Green | Color::Blue => true,
    _ => false,
};
```

#### 语义

```
- inspect 是表达式，必须有返回值
- 每个分支 => 右边的类型必须一致
- 从上到下匹配，第一个匹配的分支执行
- _ 是通配符，永远匹配
- guard (if 条件) 在模式匹配成功后额外检查
- 如果 enum 的所有 variant 都被覆盖，可以省略 _
- 如果 enum 没有全覆盖且没有 _，编译器警告
```

#### 穷举性检查（Exhaustiveness）

```
enum Color { Red, Green, Blue }

// ✅ 全覆盖
inspect (c) {
    Color::Red => 1,
    Color::Green => 2,
    Color::Blue => 3,
}

// ✅ 有通配符
inspect (c) {
    Color::Red => 1,
    _ => 0,
}

// ❌ 编译错误：未覆盖 Green, Blue
inspect (c) {
    Color::Red => 1,
}
```

#### 编译器实现要点

```
1. Parser 增强
   - 支持 Shape::Circle(r) 语法
   - 支持 Point { x, y } 语法
   - 支持 if guard
   - 支持 a..b 范围
   - 支持 | or 模式

2. TypeChecker 增强
   - 穷举性检查
   - 解构变量类型推断
   - 分支返回类型一致性检查
   - guard 条件类型检查（必须是 bool）

3. Compiler 增强
   - enum 解构 → 读 tag → 条件跳转
   - struct 解构 → 读字段偏移
   - guard → 额外条件跳转
   - 范围 → 两次比较
   - or → 多个跳转目标同一分支

4. 不需要新增 OpCode
   用现有的 Jmp/JmpFalse/Eq/Neq/Lt/Le/Gt/Ge 组合即可
```

---

### 2.4 for / break / continue

**C++ 对应：** 基础控制流，已在 Known Gaps 中
**Kinglet 理由：** 只有 while 不够，for 是最常见的循环

#### 语法

```kinglet
// 经典 for
for (int i = 0; i < 10; i += 1) {
    io::out("{}", i);
}

// for-in（遍历数组等）
for (auto item in items) {
    io::out("{}", item);
}

// for-in 带索引
for (auto item, int i in items) {
    io::out("{}: {}", i, item);
}

// break 和 continue
for (int i = 0; i < 100; i += 1) {
    if (i % 2 == 0) continue;
    if (i > 50) break;
    io::out("{}", i);
}
```

#### 编译器实现要点

```
1. for (init; cond; step) body
   编译为：
     init
     loop_start:
       cond → JmpFalse → loop_end
       body
       step
       Jmp → loop_start
     loop_end:

2. for-in 后续支持（需要迭代器协议，P3 trait 之后）

3. break = Jmp → loop_end
   continue = Jmp → step（经典 for）或 loop_start（for-in）

4. Compiler 需要维护循环栈，记录每层循环的 break/continue 跳转目标
```

---

## P3：抽象与复用

### 3.1 trait（概念/接口）

**C++ 对应：** concepts (C++20) — 受限于模板，不是类型
**Kinglet 理由：** 静态分发的接口，零开销抽象

#### 语法

```kinglet
// 声明 trait
trait Printable {
    to_string(self) -> string;
}

trait Eq {
    eq(self, other: Self) -> bool;
}

trait Hash {
    hash(self) -> int;
}

// 组合 trait
trait Comparable : Eq {
    lt(self, other: Self) -> bool;
}

// 为类型实现 trait
impl Printable for int {
    to_string(self) -> string {
        return int_to_string(self);
    }
}

impl Printable for string {
    to_string(self) -> string {
        return self;
    }
}

impl Eq for Point {
    eq(self, other: Point) -> bool {
        return self.x == other.x && self.y == other.y;
    }
}

// 用 trait 约束泛型（P3 后期或 P4）
fn print<T: Printable>(value: T) {
    io::out("{}", value.to_string());
}
```

#### 语义

```
- trait 定义一组方法签名（不含实现）
- impl ... for ... 给具体类型实现 trait
- Self 代表实现类型自身
- trait 可以组合（继承其他 trait）
- trait 方法通过 vtable 或单态化分发（实现时选）
- 一个类型可以实现多个 trait
- 不允许孤儿 impl（只能为自己项目内的类型实现自己项目内的 trait）
```

---

### 3.2 Result<T, E> 和错误处理

**C++ 对应：** std::expected (C++23) — 受限，没有 sum type 支持
**Kinglet 理由：** enum 是 ADT，Result 天然可用

#### 语法

```kinglet
// Result 是标准库内置的泛型 enum
// enum Result<T, E> { Ok(T), Err(E) }

// 函数返回 Result
int_div(int a, int b) -> Result<int, string> {
    if (b == 0) {
        return Result::Err("division by zero");
    }
    return Result::Ok(a / b);
}

// 调用方用 inspect 处理
auto result = int_div(10, 0);
inspect (result) {
    Result::Ok(value) => io::out("{}", value),
    Result::Err(msg) => io::err("error: {}", msg),
}

// 或者用 ? 运算符提前返回错误（语法糖）
int chain(int a, int b) -> Result<int, string> {
    auto x = int_div(a, b)?;     // 如果 Err，直接返回 Err
    auto y = int_div(x, 2)?;
    return Result::Ok(y);
}
```

#### ? 运算符语义

```
auto x = expr?;
等价于：
auto __tmp = expr;
inspect (__tmp) {
    Result::Ok(v) => v,        // 正常取出值，绑定到 x
    Result::Err(e) => return Result::Err(e),  // 提前返回
}
```

---

### 3.3 @derive（自动派生）

**C++ 对应：** Herb Sutter metaclasses P0707 — 长期搁置
**Kinglet 理由：** 减少样板代码，编译器自动生成 trait 实现

#### 语法

```kinglet
@derive(Eq, Format)
struct Point {
    float x;
    float y;
}

// 编译器自动生成：
// impl Eq for Point {
//     eq(self, other) -> bool {
//         return self.x == other.x && self.y == other.y;
//     }
// }
// impl Format for Point {
//     fmt(self) -> string {
//         return "({}, {})" % (self.x, self.y);
//     }
// }

// enum 也可以 derive
@derive(Eq)
enum Color {
    Red,
    Green,
    Blue,
}
```

#### 可 derive 的标准 trait

```
@derive(Eq)        → 生成 == 和 !=
@derive(Ord)       → 生成 < > <= >=（需要字段有序）
@derive(Hash)      → 生成 hash()
@derive(Format)    → 生成 fmt() 用于 {} 格式化
@derive(Clone)     → 生成显式拷贝方法
@derive(Default)   → 生成默认值（零值构造）
```

---

### 3.4 命名参数

**C++ 对应：** 多次提案被拒，与函数重载/模板交互太复杂
**Kinglet 理由：** Kinglet 没有函数重载，所以没有冲突

#### 语法

```kinglet
// 函数声明可以命名参数（和位置参数一致，本来就是命名的）
Window create_window(string title, int width, int height) {
    // ...
}

// 调用时可以按名传参
auto w = create_window(title: "Hello", width: 800, height: 600);

// 顺序无关
auto w = create_window(height: 600, width: 800, title: "Hello");

// 也可以混合
auto w = create_window("Hello", height: 600, width: 800);

// 默认值
Window create_window(string title, int width = 800, int height = 600) {
    // ...
}

auto w = create_window("Hello");              // 800x600
auto w = create_window("Hello", height: 900); // 800x900
```

#### 语义

```
- 命名参数和位置参数是同一套参数，只是调用时可以用名字指定
- 一旦用了命名参数，后续参数必须全部命名
- 命名参数不能重复
- 默认值从右往左连续
```

---

## P4+：高级特性

### 4.1 Contracts（契约）

**C++ 对应：** P2900 — 反复撤回，C++26 再次尝试
**Kinglet 理由：** 无 ABI 负担，语义可以定义清楚

#### 语法

```kinglet
// 前置条件
int divide(int a, int b) -> int
    requires b != 0
{
    return a / b;
}

// 后置条件
int abs(int x) -> int
    ensures result >= 0
{
    return inspect (x) {
        _ if x < 0 => -x,
        _ => x,
    };
}

// 不变式（struct）
struct BankAccount {
    int balance;

    invariant balance >= 0;

    deposit(int amount)
        requires amount > 0
        ensures balance > old(balance)
    {
        mut self.balance = self.balance + amount;
    }
}
```

#### 语义

```
- requires 在函数入口检查
- ensures 在函数出口检查，result 是返回值，old(expr) 是入口时的值
- invariant 在每次 struct 方法调用前后检查
- Debug 模式下检查，Release 模式下可关闭（类似 assert）
- 违反 = panic（不是 throw，不是 UB）
```

---

### 4.2 UFCS（统一函数调用）

**C++ 对应：** N4165 — 被拒
**Kinglet 理由：** 让 trait 方法可以用点号调用

#### 语法

```kinglet
// 自由函数
int length(string s) { return s.size(); }

// 两种调用方式等价
int n = length("hello");
int n = "hello".length();

// trait 实现的方法也可以用点号
impl Printable for int {
    to_string(self) -> string { ... }
}

auto s = 42.to_string();   // 等价于 to_string(42)
```

---

### 4.3 Pipeline 运算符 |>

**C++ 对应：** P2011 — 被拒
**Kinglet 理由：** 链式操作更可读

#### 语法

```kinglet
auto result = data
    |> filter(is_valid)
    |> map(transform)
    |> sort()
    |> take(10);
// 等价于 take(sort(map(filter(data, is_valid), transform)), 10)
```

---

### 4.4 async / await

**C++ 对应：** C++20 协程 — 最小化设计，标准库组件缺失严重
**Kinglet 理由：** VM 层面可以原生支持 suspend/resume

#### 语法

```kinglet
async string fetch(string url) {
    auto response = await http_get(url);
    return response.body;
}

async void main() {
    auto a = fetch("http://a.com");
    auto b = fetch("http://b.com");
    // 并发执行两个请求
    auto results = await all(a, b);
    io::out("{}", results[0]);
}
```

---

### 4.5 Reflection（反射）

**C++ 对应：** P2996 — C++26 候选但仍有大量问题
**Kinglet 理由：** VM 保留类型信息，运行时反射几乎免费

#### 语法

```kinglet
struct User {
    string name;
    int age;
}

// 运行时反射
auto info = reflect User;
io::out("type: {}", info.name());           // "User"
io::out("fields: {}", info.field_count());  // 2

for (auto field in info.fields()) {
    io::out("{}: {}", field.name(), field.type_name());
}
// 输出:
// name: string
// age: int
```

---

## 特性总览表

| 特性 | 阶段 | C++ 对应提案 | C++ 状态 | 实现难度 |
|------|------|-------------|---------|---------|
| enum (ADT) | P2 | 多次 enum 增强 | ❌ 被拒 | ★★★ |
| struct + 解构 | P2 | structured bindings | ⚠️ C++17 受限 | ★★★ |
| inspect 增强 | P2 | P2688 pattern matching | ⏳ 搁置 | ★★★ |
| for / break / continue | P2 | 基础控制流 | — | ★★☆ |
| trait | P3 | concepts (C++20) | ✅ 但受限 | ★★★★ |
| Result<T, E> | P3 | std::expected (C++23) | ✅ 但受限 | ★★☆ |
| @derive | P3 | P0707 metaclasses | ❌ 搁置 | ★★★ |
| 命名参数 | P3 | 多次提案 | ❌ 被拒 | ★★☆ |
| Contracts | P4 | P2900 | ❌ 反复撤回 | ★★★ |
| UFCS | P4 | N4165 | ❌ 被拒 | ★★★ |
| Pipeline \|> | P4 | P2011 | ❌ 被拒 | ★★☆ |
| async / await | P4 | C++20 协程 | ⚠️ 最小化 | ★★★★★ |
| Reflection | P4+ | P2996 | ⏳ C++26 候选 | ★★★★ |

---

## 实现顺序建议

```
P2.1  for / break / continue         ← 最简单，先热身
P2.2  struct + 解构                   ← enum 的前置依赖
P2.3  enum (ADT)                      ← 核心特性
P2.4  inspect 增强                    ← 依赖 struct + enum
P2.5  多函数调用                      ← 已在 Known Gaps
P2.6  NaN-boxing                      ← Value 表示优化

P3.1  trait                           ← 抽象层
P3.2  Result<T, E> + ? 运算符         ← 依赖 enum
P3.3  @derive                         ← 依赖 trait
P3.4  命名参数                        ← 独立
P3.5  命名空间 + alias (IO Phase B)   ← 已有计划
P3.6  FFI 模块加载 (using native)     ← dlopen + NativeCall

P4.1  Contracts                       ← 编译时注解
P4.2  UFCS                            ← 方法查找增强
P4.3  Pipeline |>                     ← 语法糖
P4.4  泛型                            ← 大工程
P4.5  async / await                   ← VM 协程支持
P4.6  Reflection                      ← VM 类型信息
```

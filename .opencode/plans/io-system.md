---
tags:
  - plan
  - io
  - native-functions
created: 2026-05-21
---

# I/O System Plan

Kinglet I/O 系统完整设计方案。

## 一、设计概览

```rust
// 输出
io::out("hello");                    // stdout，无换行
io::out.line("hello");               // stdout，有换行
io::out("{} is {}", name, age);      // 格式化
io::err("error: {}", msg);           // stderr，无换行
io::err.line("error: {}", msg);      // stderr，有换行

// 输入
auto s = io::in();                   // 读取一行
auto s = io::in("prompt: ");         // 带提示
auto s = io::in.secret("pw: ");      // 不回显

// alias（不推荐，但可用）
alias io;
out("hello");  // 等价于 io::out("hello")
```

## 二、命名空间

- `io` 是内置命名空间，用户不能定义新的 `io::` 成员
- 用户可以通过 `Format` trait 扩展格式化能力

## 三、alias 机制

- 语法：`alias io;`
- 语义：等价于 C++ 的 `using namespace io;`
- 作用域：文件级（在文件顶部，整个文件有效）
- 不推荐使用，但可用

## 四、out/err/in 类型

`out`/`err`/`in` 是可调用对象，同时有方法：

```rust
io::out("hello")          // operator() 调用，无换行
io::out.line("hello")     // 方法调用，有换行

io::err("error")          // operator() 调用，无换行
io::err.line("error")     // 方法调用，有换行

io::in()                  // 读取一行
io::in.secret("pw: ")    // 不回显
```

## 五、格式化占位符

- `{}` — 位置参数，按顺序填充
- `{{}}` — 转义，输出 `{` 和 `}`

```rust
io::out("{} is {} years old", name, age)  // 按顺序填充
io::out("{{not a placeholder}}")           // 输出: {not a placeholder}
```

## 六、用户扩展格式化（trait）

```rust
struct Point { x: int, y: int }

impl Format for Point {
    fn fmt(&self) -> string {
        return "({}, {})".format(self.x, self.y);
    }
}

io::out("{}", point);  // 输出: (1, 2)
```

## 七、底层 I/O 实现（C）

### 7.1 跨平台 I/O

```c
// runtime/io.c
#ifdef _WIN32
  #include <windows.h>
  void io_out(const char *msg, int len) {
      DWORD written;
      WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, len, &written, NULL);
  }
  void io_err(const char *msg, int len) {
      DWORD written;
      WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, len, &written, NULL);
  }
  int io_in(char *buf, int max_len) {
      DWORD read;
      ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, max_len, &read, NULL);
      return read;
  }
#else
  #include <unistd.h>
  void io_out(const char *msg, int len) {
      write(STDOUT_FILENO, msg, len);
  }
  void io_err(const char *msg, int len) {
      write(STDERR_FILENO, msg, len);
  }
  int io_in(char *buf, int max_len) {
      return read(STDIN_FILENO, buf, max_len);
  }
#endif
```

### 7.2 格式化实现

```c
// runtime/format.c
int format_string(char *buf, int max_len, const char *fmt, va_list args) {
    int pos = 0;
    while (*fmt && pos < max_len - 1) {
        if (*fmt == '{' && *(fmt + 1) == '}') {
            const char *arg = va_arg(args, const char *);
            int arg_len = strlen(arg);
            if (pos + arg_len >= max_len - 1) break;
            memcpy(buf + pos, arg, arg_len);
            pos += arg_len;
            fmt += 2;
        } else {
            buf[pos++] = *fmt++;
        }
    }
    buf[pos] = '\0';
    return pos;
}
```

## 八、编译流程

```
Kinglet 代码 → 字节码 → VM 执行
                          ↓
                    io::out("hello")
                          ↓
                    NativeOut 指令
                          ↓
                    VM 调用 runtime/io.c 的 io_out()
```

## 九、新增指令

| 指令 | 操作数 | 说明 |
|------|--------|------|
| `NativeOut` | 参数数量 | stdout 输出 |
| `NativeOutLine` | 参数数量 | stdout 输出 + 换行 |
| `NativeErr` | 参数数量 | stderr 输出 |
| `NativeErrLine` | 参数数量 | stderr 输出 + 换行 |
| `NativeIn` | 参数数量 | 读取 stdin |
| `NativeInSecret` | 参数数量 | 不回显读取 |

## 十、实现阶段

### Phase A: 临时方案（print）

- 实现 `print("hello")` 作为临时方案
- 快速可用，后续可替换为完整 I/O

### Phase B: 命名空间 + alias

- 实现 `::` 命名空间语法
- 实现 `alias` 关键字
- 注册内置 `io` 命名空间

### Phase C: 可调用对象 + 方法调用 + 格式化

- 实现可调用对象（支持 `()` 运算符）
- 实现方法调用（支持 `.method()`）
- 实现格式化字符串 `{}` 解析
- 实现 `Format` trait
- 实现跨平台 C 运行时库

## 十一、文件清单

| 文件 | 操作 |
|------|------|
| `src/runtime/io.c` | 新建 — 跨平台 I/O 实现 |
| `src/runtime/format.c` | 新建 — 格式化字符串实现 |
| `src/runtime/BUILD.gn` | 新建 — 构建配置 |
| `src/vm/vm.cc` | 添加 NativeOut/In 指令实现 |
| `src/vm/chunk.h` | 添加 NativeOut/In 指令定义 |
| `src/ast/ast.h` | 添加命名空间相关 AST 节点 |
| `src/parser/parser.cc` | 添加 `::` 和 `alias` 解析 |
| `src/compiler/compiler.cc` | 编译 `io::out()` 为 NativeOut |
| `src/checker/type_checker.cc` | 类型检查 `io::out()` |

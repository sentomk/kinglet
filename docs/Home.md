---
tags:
  - moc
created: 2026-05-20
---

# Kinglet

> Familiar to C++ developers from day one.
> Pattern matching as the primary control flow mechanism.
> Runtime small enough to embed, fast enough to deploy without compromise.

A C++-flavored scripting language with first-class pattern matching.

## Design

- [[Philosophy]] — 三大支柱：Familiar / Match-first / Portable
- [[Syntax]] — C++ 子集语法，去掉 footguns
- [[Pattern Matching]] — 语言的灵魂
- [[Type System]] — 渐进类型：默认推断，可选标注
- [[Traits]] — 组合优于继承
- [[Concurrency]] — spawn + channel，无 GIL
- [[Error Handling]] — Result + ? 操作符，无异常
- [[Modules & Packages]] — 文件即模块，内建包管理

## Implementation

- [[Architecture]] — 编译流水线与字节码 VM
- [[Lexer]] — Token 集合与词法分析器设计
- [[Parser]] — 递归下降，输出 AST
- [[Bytecode VM]] — 栈式 VM、NaN-boxing、GC
- [[Embeddability]] — Lua 级别的嵌入体验

## Roadmap

- [[Phase 1 - MVP]] — Lexer → Parser → Bytecode → VM → REPL
- [[Phase 2 - Types & Patterns]] — struct/enum、类型推断、穷举检查
- [[Phase 3 - Stdlib & Toolchain]] — 标准库、模块系统、包管理
- [[Phase 4 - Concurrency]] — spawn/channel/select、工作窃取调度器

## Examples

- [[Constant Folding]] — 用 Kinglet 写编译器 pass

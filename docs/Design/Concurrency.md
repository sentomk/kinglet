---
tags:
  - design
  - concurrency
created: 2026-05-20
---

# Concurrency

No GIL. True parallelism. spawn + channel model.

## spawn

Lightweight tasks scheduled on a work-stealing thread pool.

```cpp
spawn {
    heavy_computation();
};
```

Not OS threads—cooperative tasks (similar to goroutines). Creation cost is minimal.

## channel

Type-safe communication channels.

```cpp
auto ch = channel<int>(buffer: 16);

spawn {
    for (auto i : range(0, 100)) {
        ch.send(i);
    }
    ch.close();
};

for (auto val : ch) {
    println(val);
}
```

## select

Multiplexing—also a form of pattern matching.

```cpp
select {
    ch1.recv() => |val| println("ch1: ", val),
    ch2.recv() => |val| println("ch2: ", val),
    timeout(1000) => println("timed out"),
}
```

## Principles

- No shared mutable state by default
- Explicit sharing via `atomic<T>` or `mutex<T>`
- Channels are the preferred communication mechanism

## Comparison with Other Languages

| Language | Model | Issue |
|---|---|---|
| Python | GIL | False parallelism |
| Ruby | GVL | False parallelism |
| Go | goroutine + channel | Reference model for Kinglet |
| Erlang | actor + mailbox | Too heavyweight for embedding |
| Lua | Coroutines (single-threaded) | No true parallelism |

## Related

- [[Phase 4 - Concurrency]] — implementation plan
- [[Architecture]] — scheduler design

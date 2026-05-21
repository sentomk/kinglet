---
tags:
  - design
created: 2026-05-20
---

# Error Handling

No exceptions. `Result<T, E>` + `?` operator.

## Result Type

```cpp
enum Result<T, E> {
    Ok(T),
    Err(E),
}
```

## ? Operator

Automatic early return on `Err`.

```cpp
Result<int, string> parse_and_double(string s) {
    auto n = parse_int(s)?;   // Early return on Err
    return Ok(n * 2);
}
```

## Pattern Matching

```cpp
match (parse_int(input)) {
    Ok(n)  => println("got: ", n),
    Err(e) => println("error: ", e),
}
```

## Rationale

- Exceptions are implicit control flow—invisible throw points
- Performance is unpredictable—zero-cost only when not thrown
- Not pattern-matchable—catch is less expressive than match
- `Result` is an ordinary enum—all [[Pattern Matching]] capabilities apply

## Related

- [[Type System]] — enum algebraic types
- [[Pattern Matching]] — match exhaustiveness checking

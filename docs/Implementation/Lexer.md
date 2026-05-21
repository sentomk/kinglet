---
tags:
  - implementation
  - phase-1
created: 2026-05-20
---
# Lexer

Lexical analyzer — converts source character stream to token stream.

Kinglet source files use the `.kl` extension.

## Token Categories

### Keywords

```
auto  int  float  double  bool  string  void  byte
const  mut
return  if  else  for  while  break  continue
match  when
import  export  namespace
struct  enum  trait
spawn  select
true  false  null
```

### Operators

```
+  -  *  /  %
=  ==  !=  <  >  <=  >=
&&  ||  !
&  |  ^  ~  <<  >>
=>  ->  ?  ::  .  ..
+=  -=  *=  /=
```

### Delimiters

```
(  )  {  }  [  ]  ,  ;  :
```

### Literals

| Type | Examples |
|---|---|
| Decimal integer | `42`, `1_000_000` |
| Hexadecimal | `0xFF` |
| Binary | `0b1010` |
| Floating point | `3.14`, `1.0e-5` |
| String | `"hello\nworld"` |
| Character | `'a'`, `'\n'` |

### Identifiers

`[a-zA-Z_][a-zA-Z0-9_]*`

## Token Structure

```cpp
struct Token {
    TokenType type;
    string_view lexeme;   // Raw source text
    int line;
    int column;
};
```

## Implementation Notes

- Single-pass scanning, no backtracking
- Keywords distinguished from identifiers via hash table
- `_` numeric separator supported (`1_000_000`)
- Whitespace and comments skipped (`//` and `/* */`)
- Error recovery: report invalid characters and continue scanning

## Related

- [[Parser]] — downstream consumer
- [[Phase 1 - MVP]] — implementation phase

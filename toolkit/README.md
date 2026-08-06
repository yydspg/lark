# lark_toolkit — Generic Utilities

The home for small, reusable helpers so modules stop scattering their own
copies around ("utils everywhere"). Zero dependencies (standard library only).

## Components

| Header | Provides |
|--------|----------|
| `toolkit/str.h` | string utilities: `Split`, `SplitAny`, `Join`, `Trim`, `StartsWith` / `EndsWith` / `Contains`, `ToLower` / `ToUpper`, `ReplaceAll`, `ToInt64` / `ToDouble`, `IsBlank` |
| `toolkit/result.h` | `Result<T, E>` — value-or-error (incl. `Result<void, E>`) |
| `toolkit/scope.h` | `ScopeGuard` + `LARK_DEFER(...)` RAII cleanup |
| `toolkit/time.h` | monotonic `NowNanos()`, `FormatDuration` ("1.5ms", "2.1s") |
| `toolkit/hash.h` | `Fnv1a`, `Djb2`, `HashCombine` |
| `toolkit/dsl.h` | reusable DSL framework: configurable `Lexer` + recursive-descent `Parser` base with position-aware `DslError` |

Include the umbrella `toolkit/toolkit.h` to get everything.

## DSL framework (`toolkit/dsl.h`)

The shared parsing machinery behind the **column expression DSL** and the
**dag arrow DSL**. `Lexer` tokenizes identifiers, numbers, and a configurable
symbol set (multi-char symbols with longest-match); `Parser` provides
`Peek / Consume / Match / Expect / ExpectSymbol / ExpectToken` and
position-aware `DslError`. New DSLs subclass `Parser` instead of re-writing a
tokenizer:

```cpp
class MiniParser : public lark::toolkit::dsl::Parser {
 public:
  MiniParser(const std::string& src) : Parser(src, ":", {"->"}) {}
  void Parse() {
    auto name = ExpectToken(TokenKind::kIdent, "name");
    if (MatchSymbol(":")) name = ExpectToken(TokenKind::kIdent, "tag").text;
    ExpectSymbol("->", "'->'");
    auto age = ExpectToken(TokenKind::kNumber, "age");
    // ...
  }
};
```

## Usage

```cpp
#include "toolkit/toolkit.h"
using namespace lark::toolkit;

auto parts = str::Split("a,b,c", ",");          // {"a","b","c"}
if (str::StartsWith(parts[0], "a")) { /* ... */ }

Result<int> r = Result<int>::Ok(42);
if (r) use(r.value());

int total = 0;
{ LARK_DEFER(total += 1;); }                    // runs at scope exit
```

## Policy

Add new cross-cutting helpers **here** (namespace `lark::toolkit`) instead of
duplicating them inside dag / column / rpc / coro / metric / cache.

## Dependencies

None.

## Build / link

```cmake
add_subdirectory(toolkit)
target_link_libraries(my_app PRIVATE lark_toolkit)
```

Tests: `tests/test_toolkit.cpp`.

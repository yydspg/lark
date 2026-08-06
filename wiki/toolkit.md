# toolkit — Generic Utilities

Library: `liblark_toolkit` · Headers: `toolkit/include/toolkit/*`

The home for small, reusable, cross-cutting helpers so modules stop scattering
their own copies ("utils everywhere"). Zero dependencies.

## Usage

```cpp
#include "toolkit/toolkit.h"
using namespace lark::toolkit;

auto parts = str::Split("a,b,c", ",");                 // {"a","b","c"}
if (str::StartsWith(parts[0], "a")) { /* ... */ }
CHECK(str::ToInt64("-42", i) && i == -42);

Result<int> r = Result<int>::Ok(42);
if (r) use(r.value());

int total = 0;
{ LARK_DEFER(total += 1;); }                            // runs at scope exit

auto ns = time::NowNanos();
std::cout << time::FormatDuration(ns);                  // "1.5ms" / "2.1s"

uint64_t h = hash::Fnv1a("user:42");
uint64_t k = hash::HashCombine(h, 7);
```

### DSL framework (shared lexer/parser)

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

The column expression DSL and the dag arrow DSL both build on this framework.

## Caveats

- **`Result` is private-constructed**: only `Result::Ok(...)` / `Result::Err(...)`
  create one; `value()` throws on error, `value_or` never throws.
- **`ScopeGuard` / `LARK_DEFER`** run on scope exit unless `Release()` is called;
  do not `Release` twice.
- **`str::ToInt64/ToDouble` are strict** — they parse the entire string and
  reject trailing garbage (`"12x"` fails).
- **`Trim`** is ASCII-only (uses `std::isspace`).
- **DSL**: `#` starts a comment to end of line; multi-char symbols are matched
  longest-first; the `Lexer` copies the source so it can outlive the caller
  string. `DslError` carries line/col.
- **Policy**: coroutine-dependent utilities (pool/`Future`) live in **`coro`**
  (e.g. `coro::batch`), not here — `toolkit` is zero-dependency and `coro`
  already links it, so a toolkit→coro edge would be a cycle.

## Implementation

- **`str`**: `Split`/`Join`/`Trim`/case/`ReplaceAll` on `std::string_view`;
  numeric parsing via `strtoll`/`strtod` with end-pointer validation.
- **`Result<T,E>`**: optional value + optional error; private default ctor
  forces the `Ok/Err` factories. `Result<void,E>` specialization.
- **`ScopeGuard`**: stores a `std::function` + an armed flag; move transfers
  ownership; `Release` disarms the destructor.
- **`time`**: `NowNanos()` from `steady_clock`; `FormatDuration` picks ns/us/ms/s
  by magnitude.
- **`hash`**: FNV-1a / djb2 / boost-style `HashCombine`.
- **`dsl`**: `Lexer` tokenizes identifiers, numbers, 1-char symbols and
  multi-char symbols (longest first) while tracking line/col; `Parser` provides
  `Peek/Consume/Match/Expect/ExpectSymbol/ExpectToken` over a one-token lookahead
  and throws position-aware `DslError`.

## Architecture

```
toolkit (zero deps) ──▶ consumed by metric → coro → dag / column / cache / rpc
   ├─ str / result / scope / time / hash   (general utilities)
   └─ dsl                                (shared parsing framework)
```

`toolkit` is the bottom-most shared module: everything above it may depend on
it, it may depend on nothing.

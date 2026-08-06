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

Include the umbrella `toolkit/toolkit.h` to get everything.

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

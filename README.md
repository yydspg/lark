# LARK - C++20 Framework

<div align="center">

**A C++20 framework for high-performance async systems: coroutine primitives, DAG business orchestration, a columnar compute engine, RPC, monitoring and caching**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

</div>

---

## 📖 Documentation

- 📚 **[README](docs/README_EN.md)** - Full project documentation
- 📘 **[Usage Guide](docs/USAGE_EN.md)** - Detailed usage examples and best practices

## Features

- **Coroutine primitives + orchestration** (`coro`) — thread pool / async event
  / task primitives, CompletableFuture-style `Future<T>` orchestration
  (Then / ThenAsync / AllOf / AnyOf / co_await), lock-free `Context` +
  `Pipeline` step orchestration, differentiated pools
  (compute / io / lowload / dag / daemon), and compute auto-tuning with CPU
  pinning.
- **DAG orchestration** (`dag`) — classic Node layer (coroutine scheduling,
  per-node timing, batch-disable, graceful degradation) plus a business-oriented
  **Op layer**: subclass `Op` with only business logic, cross-cutting
  `OpAspect` (aspect) for condition-based auto-skip, all ops async on the dag pool.
- **Column engine** (`column`) — TensorFlow/ggml-inspired feed / compute /
  fetch pipeline with DSL modules, multi-dtype tensors and Q8_0 quantization.
- **RPC** (`rpc`) — transport-agnostic framework (in-process / gRPC / brpc).
- **Monitoring** (`metric`) — unified pluggable Monitor + flame-graph
  profiling and null/anomaly probes.
- **Cache** (`cache`) — abstract cache with local and remote implementations.
- **Toolkit** (`toolkit`) — shared utilities (string, result, RAII, hash, time).

## Project Structure (multi-module)

The framework is a CMake parent project composed of **seven** independent
subprojects, each with its own `CMakeLists.txt` and `include/` + `src/`,
dependencies declared via `target_link_libraries` (Maven/Gradle-style modules):

```
lark/                       ← parent
├─ CMakeLists.txt           ← project() + add_subdirectory(...) + aggregate `lark`
├─ coro/    → liblark_coro   standalone coroutine primitives
├─ dag/     → liblark_dag    DAG business execution framework (depends on coro)
├─ column/  → liblark_column feed/compute/fetch column engine (depends on coro)
├─ rpc/     → liblark_rpc    generic RPC framework (gRPC/brpc/inproc wrappers)
├─ metric/  → liblark_metric unified monitoring + flame graph / probes
├─ cache/   → liblark_cache  abstract cache (local / remote) + factory
├─ toolkit/ → liblark_toolkit generic utilities (string, result, scope, hash, time)
├─ examples/
└─ tests/
```

Dependency graph: `metric → toolkit`; `coro → metric + toolkit`; `dag → coro +
metric + toolkit`; `column → coro + metric`; `rpc → metric`; `cache → metric +
rpc`. Link the aggregate `lark` target or any individual library.

## Build & test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build            # all module test suites
```

Optional RPC backends: `-DLARK_WITH_GRPC=ON` / `-DLARK_WITH_BRPC=ON`.

## Subproject READMEs

| Subproject | Library | README |
|------------|---------|--------|
| `coro/` | liblark_coro | [coro/README.md](coro/README.md) |
| `dag/` | liblark_dag | [dag/README.md](dag/README.md) |
| `column/` | liblark_column | [column/README.md](column/README.md) |
| `rpc/` | liblark_rpc | [rpc/README.md](rpc/README.md) |
| `metric/` | liblark_metric | [metric/README.md](metric/README.md) |
| `cache/` | liblark_cache | [cache/README.md](cache/README.md) |
| `toolkit/` | liblark_toolkit | [toolkit/README.md](toolkit/README.md) |

## Other Documentation

- 📄 [Contributing Guide](CONTRIBUTING.md)
- 📄 [Auto-Registration Guide](docs/AUTO_REGISTRATION.md)
- 📄 [Column Engine v4](docs/COLUMN_ENGINE_V4.md)
- 📄 [RPC Framework & DAG Upgrades](docs/RPC_AND_DAG_UPGRADES.md)
- 📄 [Metric & Cache](docs/METRIC_AND_CACHE.md)
- 📄 [License](LICENSE) (MIT)

---

<div align="center">

**Made with ❤️ by the LARK team**

</div>

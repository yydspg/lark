# LARK - C++20 DAG Framework

<div align="center">

**A high-performance C++20 DAG (Directed Acyclic Graph) framework with coroutine-based async execution**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

</div>

---

## 📖 Documentation

- 📚 **[README](docs/README_EN.md)** - Full project documentation
- 📘 **[Usage Guide](docs/USAGE_EN.md)** - Detailed usage examples and best practices

### Project Structure (multi-module)

The framework is a CMake parent project composed of four independent subprojects:

```
lark/                       ← parent
├─ CMakeLists.txt           ← project() + add_subdirectory(...) + aggregate `lark`
├─ coro/    → liblark_coro   standalone coroutine primitives
├─ dag/     → liblark_dag    DAG business execution framework (depends on coro)
├─ column/  → liblark_column feed/compute/fetch column engine (depends on coro)
├─ rpc/     → liblark_rpc    generic RPC framework (gRPC/brpc/inproc wrappers)
├─ monitor/ → liblark_monitor unified, pluggable monitoring abstraction
├─ cache/   → liblark_cache  abstract cache (local / remote) + factory
├─ examples/
└─ tests/
```

Each subproject has its own `CMakeLists.txt` and `include/` + `src/`, with
dependencies declared via `target_link_libraries` (like Maven/Gradle modules).
Link the aggregate `lark` target or any individual library.

### Other Documentation

- 📄 [Contributing Guide](CONTRIBUTING.md)
- 📄 [Auto-Registration Guide](docs/AUTO_REGISTRATION.md)
- 📄 [Column Engine v4](docs/COLUMN_ENGINE_V4.md)
- 📄 [RPC Framework & DAG Upgrades](docs/RPC_AND_DAG_UPGRADES.md)
- 📄 [Monitor & Cache](docs/MONITOR_AND_CACHE.md)
- 📄 [License](LICENSE) (MIT)

---

<div align="center">

**Made with ❤️ by the LARK team**

</div>

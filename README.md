# LARK - C++20 DAG Framework

<div align="center">

**A high-performance C++20 DAG (Directed Acyclic Graph) framework with coroutine-based async execution**

**一个基于 C++20 协程的高性能 DAG（有向无环图）框架，支持全异步执行**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

## 📖 Documentation / 文档

### Choose your language / 选择你的语言

| Language | README | Usage Guide |
|----------|--------|-------------|
| 🇬🇧 **English** | [README_EN.md](docs/README_EN.md) | [USAGE_EN.md](docs/USAGE_EN.md) |
| 🇨🇳 **中文** | [README_ZH.md](docs/README_ZH.md) | [USAGE_ZH.md](docs/USAGE_ZH.md) |

---

### Quick Links / 快速链接

- 📚 **[English README](docs/README_EN.md)** - Full project documentation in English
- 📚 **[中文 README](docs/README_ZH.md)** - 完整中文项目文档
- 📘 **[English Usage Guide](docs/USAGE_EN.md)** - Detailed usage examples and best practices
- 📘 **[中文使用指南](docs/USAGE_ZH.md)** - 详细使用示例和最佳实践

### Project Structure / 项目结构（多子项目 / multi-module）

The framework is a CMake parent project composed of four independent subprojects
（母项目由四个独立子项目组成）:

```
lark/                       ← parent (母项目)
├─ CMakeLists.txt           ← project() + add_subdirectory(...) + aggregate `lark`
├─ coro/    → liblark_coro   standalone coroutine primitives (协程)
├─ dag/     → liblark_dag    DAG business execution framework (依赖 coro)
├─ column/  → liblark_column feed/compute/fetch column engine (依赖 coro)
├─ rpc/     → liblark_rpc    generic RPC framework (gRPC/brpc/inproc 包装)
├─ examples/
└─ tests/
```

Each subproject has its own `CMakeLists.txt` and `include/` + `src/`, with
dependencies declared via `target_link_libraries` (like Maven/Gradle modules).
Link the aggregate `lark` target or any individual library.

### Other Documentation / 其他文档

- 📄 [Contributing Guide / 贡献指南](CONTRIBUTING.md) (Bilingual / 双语)
- 📄 [Auto-Registration Guide / 自动注册指南](docs/AUTO_REGISTRATION.md) (Bilingual / 双语)
- 📄 [Column Engine v4 / 列示计算引擎](docs/COLUMN_ENGINE_V4.md) (Bilingual / 双语)
- 📄 [RPC Framework & DAG Upgrades / RPC 框架与 DAG 升级](docs/RPC_AND_DAG_UPGRADES.md) (Bilingual / 双语)
- 📄 [License / 许可证](LICENSE) (MIT)

</div>

---

<div align="center">

**Made with ❤️ by the LARK team**

</div>

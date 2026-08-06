# LARK - C++20 DAG Framework

<div align="center">

**A high-performance C++20 DAG (Directed Acyclic Graph) framework with coroutine-based async execution**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()



</div>

---

## 🌟 Overview

LARK is a modern C++20 DAG framework that provides fully asynchronous, coroutine-based execution for complex workflow orchestration. It features automatic node registration, multi-pool thread scheduling, and zero external dependencies.

### Key Features

- 🚀 **C++20 Coroutines** - Lock-free async execution with symmetric transfer
- 🔄 **Auto-Registration** - Spring-like `LARK_NODE` macro for zero-boilerplate node registration
- 🎯 **Three Pool Types** - Compute, IO, and Background pools for optimal resource utilization
- 🔒 **Type-Safe Context** - Thread-safe, type-erased data passing between nodes
- 📊 **DAG Validation** - Automatic cycle detection and dependency resolution
- 🛡️ **Fallback Support** - Graceful degradation on node failures
- 📈 **Monitoring** - Built-in observability hooks for metrics and tracing
- ⚡ **Zero Dependencies** - Pure C++20 standard library only

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Business Code                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ FetchUserNode│  │FetchOrdersNode│  │  RankNode    │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │ LARK_NODE()     │ LARK_NODE()     │ LARK_NODE() │
└─────────┼─────────────────┼─────────────────┼──────────────┘
          │                 │                 │
          ▼                 ▼                 ▼
┌─────────────────────────────────────────────────────────────┐
│                   NodeRegistry (Global)                     │
│              Auto-registration at startup                   │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                      GraphBuilder                           │
│    • Validates DAG structure                                │
│    • Detects cycles (iterative DFS)                         │
│    • Wires dependencies                                     │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                        Graph                                │
│    • Immutable, validated DAG                               │
│    • Owns all Node instances                                │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                       Executor                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │Compute Pool  │  │   IO Pool    │  │Background Pool│     │
│  │(CPU-bound)   │  │(I/O-bound)   │  │(Logging, etc)│     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│                                                             │
│    • Coroutine-based scheduling                             │
│    • Parallel dependency execution                          │
│    • Fallback on failure                                    │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                      IContext                               │
│    • Type-erased data bag                                   │
│    • Domain context injection                               │
│    • Thread-safe (business code contract)                   │
└─────────────────────────────────────────────────────────────┘
```

### Core Components

#### 1. **Node** (`node.h`)
Abstract base class for all business logic. Override `Compute()` for sync work or `Run()` for async work.

```cpp
class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto user = fetch_user_from_db();
    lark::Set(ctx, "user", user);
  }
};
LARK_NODE("fetch_user", FetchUserNode);  // Auto-registered!
```

#### 2. **IContext** (`i_context.h`)
Type-safe data passing between nodes. Supports keyed data and domain contexts.

```cpp
// Set data
lark::Set(ctx, "user_id", 42);

// Get data
auto user_id = lark::Get<int>(ctx, "user_id");

// Domain context
struct RequestDomain { std::string request_id; };
lark::ProvideDomain<RequestDomain>(ctx, "req-123");
auto& req = lark::RequireDomain<RequestDomain>(ctx);
```

#### 3. **GraphBuilder** (`graph_builder.h`)
Builds and validates DAG from node definitions.

```cpp
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"rank", {"fetch_user", "fetch_orders"}},  // dependencies
});
```

#### 4. **Executor** (`executor.h`)
Executes the graph with three thread pools.

```cpp
lark::Executor executor(
    4,   // compute threads (CPU-bound)
    8,   // IO threads (network, disk)
    2    // background threads (logging)
);
executor.Execute(*graph, ctx);
```

#### 5. **Schedule** (`schedule.h`)
Orchestration primitives for pool hopping.

```cpp
lark::Task<void> MyNode::Run(lark::IContext& ctx) override {
  co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
  heavy_cpu_work();
  co_await lark::Schedule(ctx, lark::PoolKind::kIo);
  co_await network_call();
  co_return;
}
```

### Quick Start

#### 1. Define Nodes

```cpp
#include "dag/dag.h"

class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    std::this_thread::sleep_for(40ms);
    lark::Set(ctx, "user", UserProfile{"john", 3});
  }
};
LARK_NODE("fetch_user", FetchUserNode);

class RankNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto user = lark::Get<UserProfile>(ctx, "user");
    double score = user ? user->level * 10.0 : 0.0;
    lark::Set(ctx, "score", score);
  }
};
LARK_NODE("rank", RankNode);
```

#### 2. Build and Execute

```cpp
int main() {
  // Build graph
  lark::GraphBuilder builder;
  auto graph = builder.Build({
      {"fetch_user"},
      {"rank", {"fetch_user"}},
  });

  // Setup context
  lark::DefaultContext ctx;

  // Execute
  lark::Executor executor;
  executor.Execute(*graph, ctx);

  // Get result
  auto score = lark::Get<double>(ctx, "score");
  std::cout << "Score: " << (score ? *score : 0.0) << "\n";

  return 0;
}
```

### Build

```bash
# Configure
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build -j

# Run tests
./build/tests/dag_tests

# Run example
./build/examples/simple_pipeline
```

### Requirements

- C++20 compatible compiler (Clang 16+, GCC 11+, MSVC 19.29+)
- CMake 3.20+
- No external dependencies

### License

LARK is licensed under the MIT License. See [LICENSE](LICENSE) for details.

```text
MIT License

Copyright (c) 2024 LARK Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

---

<div align="center">


</div>


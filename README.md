# LARK - C++20 DAG Framework

<div align="center">

**A high-performance C++20 DAG (Directed Acyclic Graph) framework with coroutine-based async execution**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

[English](#english) | [中文](#中文)

</div>

---

<a id="english"></a>
## 🌟 English

### Overview

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

<a id="中文"></a>
## 🌟 中文

### 概述

LARK 是一个现代化的 C++20 DAG（有向无环图）框架，提供基于协程的全异步执行能力，用于复杂工作流编排。它具有自动节点注册、多线程池调度和零外部依赖的特性。

### 核心特性

- 🚀 **C++20 协程** - 基于对称传输的无锁异步执行
- 🔄 **自动注册** - 类似 Spring 的 `LARK_NODE` 宏，零样板代码注册
- 🎯 **三种线程池** - 计算池、IO 池和后台池，优化资源利用
- 🔒 **类型安全上下文** - 节点间类型擦除的数据传递
- 📊 **DAG 验证** - 自动环检测和依赖解析
- 🛡️ **降级支持** - 节点失败时的优雅降级
- 📈 **监控能力** - 内置可观测性钩子，支持指标和追踪
- ⚡ **零依赖** - 纯 C++20 标准库实现

### 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                        业务代码层                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ FetchUserNode│  │FetchOrdersNode│  │  RankNode    │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │ LARK_NODE()     │ LARK_NODE()     │ LARK_NODE() │
└─────────┼─────────────────┼─────────────────┼──────────────┘
          │                 │                 │
          ▼                 ▼                 ▼
┌─────────────────────────────────────────────────────────────┐
│                   NodeRegistry (全局注册表)                   │
│                    程序启动时自动注册                          │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                      GraphBuilder                           │
│    • 验证 DAG 结构                                          │
│    • 检测环（迭代 DFS）                                      │
│    • 连接依赖关系                                            │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                        Graph                                │
│    • 不可变的、已验证的 DAG                                   │
│    • 拥有所有 Node 实例                                       │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                       Executor                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │  计算池       │  │   IO 池      │  │  后台池       │     │
│  │(CPU密集型)    │  │(IO密集型)    │  │(日志等)       │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│                                                             │
│    • 基于协程的调度                                          │
│    • 并行执行依赖节点                                        │
│    • 失败时降级处理                                          │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                      IContext                               │
│    • 类型擦除的数据容器                                       │
│    • 领域上下文注入                                          │
│    • 线程安全（业务代码契约）                                  │
└─────────────────────────────────────────────────────────────┘
```

### 核心组件

#### 1. **Node** (`node.h`)
所有业务逻辑的抽象基类。重写 `Compute()` 处理同步工作，或重写 `Run()` 处理异步工作。

```cpp
class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto user = fetch_user_from_db();
    lark::Set(ctx, "user", user);
  }
};
LARK_NODE("fetch_user", FetchUserNode);  // 自动注册！
```

#### 2. **IContext** (`i_context.h`)
节点间类型安全的数据传递。支持键值数据和领域上下文。

```cpp
// 设置数据
lark::Set(ctx, "user_id", 42);

// 获取数据
auto user_id = lark::Get<int>(ctx, "user_id");

// 领域上下文
struct RequestDomain { std::string request_id; };
lark::ProvideDomain<RequestDomain>(ctx, "req-123");
auto& req = lark::RequireDomain<RequestDomain>(ctx);
```

#### 3. **GraphBuilder** (`graph_builder.h`)
从节点定义构建和验证 DAG。

```cpp
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"rank", {"fetch_user", "fetch_orders"}},  // 依赖关系
});
```

#### 4. **Executor** (`executor.h`)
使用三个线程池执行图。

```cpp
lark::Executor executor(
    4,   // 计算线程（CPU密集型）
    8,   // IO线程（网络、磁盘）
    2    // 后台线程（日志等）
);
executor.Execute(*graph, ctx);
```

#### 5. **Schedule** (`schedule.h`)
线程池切换的编排原语。

```cpp
lark::Task<void> MyNode::Run(lark::IContext& ctx) override {
  co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
  heavy_cpu_work();
  co_await lark::Schedule(ctx, lark::PoolKind::kIo);
  co_await network_call();
  co_return;
}
```

### 快速开始

#### 1. 定义节点

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

#### 2. 构建并执行

```cpp
int main() {
  // 构建图
  lark::GraphBuilder builder;
  auto graph = builder.Build({
      {"fetch_user"},
      {"rank", {"fetch_user"}},
  });

  // 设置上下文
  lark::DefaultContext ctx;

  // 执行
  lark::Executor executor;
  executor.Execute(*graph, ctx);

  // 获取结果
  auto score = lark::Get<double>(ctx, "score");
  std::cout << "分数: " << (score ? *score : 0.0) << "\n";

  return 0;
}
```

### 构建项目

```bash
# 配置
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++

# 构建
cmake --build build -j

# 运行测试
./build/tests/dag_tests

# 运行示例
./build/examples/simple_pipeline
```

### 系统要求

- 支持 C++20 的编译器（Clang 16+、GCC 11+、MSVC 19.29+）
- CMake 3.20+
- 无外部依赖

### 许可证

LARK 采用 MIT 开源协议。详情请查看 [LICENSE](LICENSE)。

```text
MIT 许可证

版权所有 (c) 2024 LARK 贡献者

特此免费授予任何获得本软件及相关文档文件（“软件”）副本的人，
不受限制地处理本软件，包括但不限于使用、复制、修改、合并、发布、
分发、再许可和/或销售本软件副本的权利，但须符合以下条件：

上述版权声明和本许可声明应包含在本软件的所有副本或重要部分中。
```

---

<div align="center">

**Made with ❤️ by the LARK team**

[Back to Top](#lark---c20-dag-framework)

</div>

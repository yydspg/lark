# LARK - C++20 DAG 框架

<div align="center">

**一个基于 C++20 协程的高性能 DAG（有向无环图）框架，支持全异步执行**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

[返回主页](README.md) | [English Version](README_EN.md)

**[使用指南](USAGE_ZH.md)** | **[Usage Guide](USAGE_EN.md)**

</div>

---

## 🌟 概述

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

特此免费授予任何获得本软件及相关文档文件（"软件"）副本的人，
不受限制地处理本软件，包括但不限于使用、复制、修改、合并、发布、
分发、再许可和/或销售本软件副本的权利，但须符合以下条件：

上述版权声明和本许可声明应包含在本软件的所有副本或重要部分中。
```

---

<div align="center">

**[返回页首](#lark---c20-dag-框架)** | **[English Version](README_EN.md)** | **[主页](README.md)**

</div>

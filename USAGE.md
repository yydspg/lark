# LARK Usage Guide / 使用指南

<div align="center">

[English](#english-usage-guide) | [中文](#中文使用指南)

</div>

---

<a id="english-usage-guide"></a>
## 📘 English Usage Guide

### Table of Contents

1. [Installation](#installation)
2. [Basic Concepts](#basic-concepts)
3. [Creating Nodes](#creating-nodes)
4. [Building Graphs](#building-graphs)
5. [Executing Graphs](#executing-graphs)
6. [Advanced Features](#advanced-features)
7. [Best Practices](#best-practices)
8. [Common Patterns](#common-patterns)

---

### Installation

#### CMake Integration

```cmake
# FetchContent (recommended)
include(FetchContent)
FetchContent_Declare(
  lark
  GIT_REPOSITORY https://github.com/yydspg/lark.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(lark)

target_link_libraries(your_target PRIVATE lark)
```

#### Manual Build

```bash
git clone https://github.com/yydspg/lark.git
cd lark
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
sudo cmake --install build
```

---

### Basic Concepts

#### Node

A **Node** represents a unit of work in your DAG. Each node can:
- Read data from the context
- Write data to the context
- Depend on other nodes
- Handle failures with fallback logic

#### Context

The **Context** is a type-safe data bag shared by all nodes. It supports:
- **Keyed data**: `Set(ctx, "key", value)` / `Get<T>(ctx, "key")`
- **Domain contexts**: Type-safe structured data injection

#### Graph

A **Graph** is a validated DAG of nodes. It ensures:
- No circular dependencies
- All dependencies are satisfied
- Topological execution order

#### Executor

The **Executor** runs the graph using three thread pools:
- **Compute pool**: CPU-bound tasks
- **IO pool**: Network/disk I/O tasks
- **Background pool**: Low-priority tasks (logging, metrics)

---

### Creating Nodes

#### Simple Synchronous Node

```cpp
#include "dag/dag.h"

class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    // Simulate work
    std::this_thread::sleep_for(40ms);
    
    // Write to context
    lark::Set(ctx, "user", UserProfile{"john", 3});
  }
};
LARK_NODE("fetch_user", FetchUserNode);
```

#### Async Node with Pool Hopping

```cpp
class ProcessDataNode : public lark::Node {
  lark::Task<void> Run(lark::IContext& ctx) override {
    // Start on IO pool (default)
    auto data = co_await fetch_from_network();
    
    // Hop to compute pool for CPU work
    co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
    auto result = heavy_processing(data);
    
    // Hop back to IO pool
    co_await lark::Schedule(ctx, lark::PoolKind::kIo);
    co_await save_to_database(result);
    
    co_return;
  }
};
LARK_NODE("process_data", ProcessDataNode);
```

#### Node with Fallback

```cpp
class RiskSignalNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    // This might fail
    auto risk = call_external_service();
    lark::Set(ctx, "risk", risk);
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr error) override {
    // Provide degraded result
    lark::Set(ctx, "risk", 0.5);  // safe default
    return true;  // recovered
  }
};
LARK_NODE("risk_signal", RiskSignalNode);
```

---

### Building Graphs

#### Basic Graph

```cpp
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"rank", {"fetch_user", "fetch_orders"}},
});
```

#### Complex Graph with IDs

```cpp
auto graph = builder.Build({
    {"fetch_user", {}, "user_v1"},
    {"fetch_user", {}, "user_v2"},  // same type, different id
    {"aggregate", {"user_v1", "user_v2"}},
});
```

#### Graph Validation

The builder automatically validates:
- ✅ No circular dependencies
- ✅ All dependencies exist
- ✅ No duplicate node IDs

```cpp
try {
  auto graph = builder.Build({
      {"a", {"b"}},
      {"b", {"a"}},  // cycle!
  });
} catch (const std::runtime_error& e) {
  std::cerr << "Invalid graph: " << e.what() << "\n";
}
```

---

### Executing Graphs

#### Basic Execution

```cpp
lark::DefaultContext ctx;
lark::Executor executor;
executor.Execute(*graph, ctx);

// Read results
auto result = lark::Get<int>(ctx, "result");
```

#### Custom Pool Sizes

```cpp
lark::Executor executor(
    4,   // compute threads
    8,   // IO threads
    2    // background threads
);
executor.Execute(*graph, ctx);
```

#### Adding Monitoring

```cpp
class MyMonitor : public lark::Monitor {
  void OnNodeSuccess(const lark::Node& node, 
                     std::chrono::nanoseconds elapsed) override {
    metrics.record_success(node.id(), elapsed);
  }
  
  void OnNodeFailure(const lark::Node& node,
                     std::exception_ptr error,
                     std::chrono::nanoseconds elapsed) override {
    metrics.record_failure(node.id(), error);
  }
};

executor.SetMonitor(std::make_shared<MyMonitor>());
executor.Execute(*graph, ctx);
```

---

### Advanced Features

#### Domain Contexts

Inject structured data into the context:

```cpp
struct RequestDomain {
  std::string request_id;
  int64_t user_id;
};

// Inject
lark::ProvideDomain<RequestDomain>(ctx, "req-123", 42LL);

// Access
auto& req = lark::RequireDomain<RequestDomain>(ctx);
std::cout << "Request: " << req.request_id << "\n";
```

#### Parallel Execution

Nodes with no dependencies run in parallel automatically:

```cpp
auto graph = builder.Build({
    {"fetch_user"},      // ┐
    {"fetch_orders"},    // ├─> These run in parallel
    {"fetch_products"},  // ┘
    {"rank", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

#### Graph Reuse

Execute the same graph multiple times with different contexts:

```cpp
lark::Executor executor;

for (const auto& request : requests) {
  lark::DefaultContext ctx;
  lark::ProvideDomain<RequestDomain>(ctx, request.id, request.user_id);
  
  executor.Execute(*graph, ctx);  // Reuses graph structure
  
  process_result(ctx);
}
```

---

### Best Practices

#### 1. One Node Per File

```cpp
// fetch_user_node.cpp
class FetchUserNode : public lark::Node { ... };
LARK_NODE("fetch_user", FetchUserNode);
```

#### 2. Use Descriptive Names

```cpp
LARK_NODE("fetch_user_profile", FetchUserProfileNode);  // ✓ Good
LARK_NODE("node1", FetchUserProfileNode);               // ✗ Bad
```

#### 3. Handle Failures Gracefully

```cpp
class ExternalServiceNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto result = call_service();  // might throw
    lark::Set(ctx, "result", result);
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr) override {
    lark::Set(ctx, "result", DefaultValue{});  // safe fallback
    return true;
  }
};
```

#### 4. Use Type-Safe Context

```cpp
// ✓ Good: Type-safe
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<int>(ctx, "user_id");

// ✗ Bad: Type mismatch
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<std::string>(ctx, "user_id");  // nullptr!
```

#### 5. Choose the Right Pool

```cpp
// CPU-intensive work
co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
process_data();

// I/O work
co_await lark::Schedule(ctx, lark::PoolKind::kIo);
fetch_from_network();

// Background work
co_await lark::Schedule(ctx, lark::PoolKind::kBackground);
log_metrics();
```

---

### Common Patterns

#### Pattern 1: Fan-Out / Fan-In

```cpp
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"fetch_products"},
    {"aggregate", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

#### Pattern 2: Pipeline

```cpp
auto graph = builder.Build({
    {"extract"},
    {"transform", {"extract"}},
    {"load", {"transform"}},
});
```

#### Pattern 3: Conditional Branch

```cpp
auto graph = builder.Build({
    {"check_condition"},
    {"path_a", {"check_condition"}},
    {"path_b", {"check_condition"}},
    {"merge", {"path_a", "path_b"}},
});
```

#### Pattern 4: Retry with Fallback

```cpp
class RetryNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    for (int i = 0; i < 3; ++i) {
      try {
        auto result = risky_operation();
        lark::Set(ctx, "result", result);
        return;
      } catch (...) {
        if (i == 2) throw;  // last attempt
      }
    }
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr) override {
    lark::Set(ctx, "result", DefaultValue{});
    return true;
  }
};
```

---

<a id="中文使用指南"></a>
## 📘 中文使用指南

### 目录

1. [安装](#安装)
2. [基本概念](#基本概念)
3. [创建节点](#创建节点)
4. [构建图](#构建图)
5. [执行图](#执行图)
6. [高级特性](#高级特性)
7. [最佳实践](#最佳实践)
8. [常见模式](#常见模式)

---

### 安装

#### CMake 集成

```cmake
# FetchContent（推荐）
include(FetchContent)
FetchContent_Declare(
  lark
  GIT_REPOSITORY https://github.com/yydspg/lark.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(lark)

target_link_libraries(your_target PRIVATE lark)
```

#### 手动构建

```bash
git clone https://github.com/yydspg/lark.git
cd lark
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
sudo cmake --install build
```

---

### 基本概念

#### 节点（Node）

**节点**代表 DAG 中的一个工作单元。每个节点可以：
- 从上下文读取数据
- 向上下文写入数据
- 依赖其他节点
- 使用降级逻辑处理失败

#### 上下文（Context）

**上下文**是所有节点共享的类型安全数据容器。它支持：
- **键值数据**：`Set(ctx, "key", value)` / `Get<T>(ctx, "key")`
- **领域上下文**：类型安全的结构化数据注入

#### 图（Graph）

**图**是一个经过验证的节点 DAG。它确保：
- 无循环依赖
- 所有依赖都得到满足
- 拓扑执行顺序

#### 执行器（Executor）

**执行器**使用三个线程池运行图：
- **计算池**：CPU 密集型任务
- **IO 池**：网络/磁盘 I/O 任务
- **后台池**：低优先级任务（日志、指标）

---

### 创建节点

#### 简单同步节点

```cpp
#include "dag/dag.h"

class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    // 模拟工作
    std::this_thread::sleep_for(40ms);
    
    // 写入上下文
    lark::Set(ctx, "user", UserProfile{"john", 3});
  }
};
LARK_NODE("fetch_user", FetchUserNode);
```

#### 带线程池切换的异步节点

```cpp
class ProcessDataNode : public lark::Node {
  lark::Task<void> Run(lark::IContext& ctx) override {
    // 从 IO 池开始（默认）
    auto data = co_await fetch_from_network();
    
    // 切换到计算池处理 CPU 密集工作
    co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
    auto result = heavy_processing(data);
    
    // 切换回 IO 池
    co_await lark::Schedule(ctx, lark::PoolKind::kIo);
    co_await save_to_database(result);
    
    co_return;
  }
};
LARK_NODE("process_data", ProcessDataNode);
```

#### 带降级的节点

```cpp
class RiskSignalNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    // 可能会失败
    auto risk = call_external_service();
    lark::Set(ctx, "risk", risk);
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr error) override {
    // 提供降级结果
    lark::Set(ctx, "risk", 0.5);  // 安全默认值
    return true;  // 已恢复
  }
};
LARK_NODE("risk_signal", RiskSignalNode);
```

---

### 构建图

#### 基本图

```cpp
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"rank", {"fetch_user", "fetch_orders"}},
});
```

#### 带 ID 的复杂图

```cpp
auto graph = builder.Build({
    {"fetch_user", {}, "user_v1"},
    {"fetch_user", {}, "user_v2"},  // 相同类型，不同 ID
    {"aggregate", {"user_v1", "user_v2"}},
});
```

#### 图验证

构建器自动验证：
- ✅ 无循环依赖
- ✅ 所有依赖都存在
- ✅ 无重复节点 ID

```cpp
try {
  auto graph = builder.Build({
      {"a", {"b"}},
      {"b", {"a"}},  // 循环！
  });
} catch (const std::runtime_error& e) {
  std::cerr << "无效的图: " << e.what() << "\n";
}
```

---

### 执行图

#### 基本执行

```cpp
lark::DefaultContext ctx;
lark::Executor executor;
executor.Execute(*graph, ctx);

// 读取结果
auto result = lark::Get<int>(ctx, "result");
```

#### 自定义池大小

```cpp
lark::Executor executor(
    4,   // 计算线程
    8,   // IO 线程
    2    // 后台线程
);
executor.Execute(*graph, ctx);
```

#### 添加监控

```cpp
class MyMonitor : public lark::Monitor {
  void OnNodeSuccess(const lark::Node& node, 
                     std::chrono::nanoseconds elapsed) override {
    metrics.record_success(node.id(), elapsed);
  }
  
  void OnNodeFailure(const lark::Node& node,
                     std::exception_ptr error,
                     std::chrono::nanoseconds elapsed) override {
    metrics.record_failure(node.id(), error);
  }
};

executor.SetMonitor(std::make_shared<MyMonitor>());
executor.Execute(*graph, ctx);
```

---

### 高级特性

#### 领域上下文

向上下文注入结构化数据：

```cpp
struct RequestDomain {
  std::string request_id;
  int64_t user_id;
};

// 注入
lark::ProvideDomain<RequestDomain>(ctx, "req-123", 42LL);

// 访问
auto& req = lark::RequireDomain<RequestDomain>(ctx);
std::cout << "请求: " << req.request_id << "\n";
```

#### 并行执行

无依赖的节点自动并行运行：

```cpp
auto graph = builder.Build({
    {"fetch_user"},      // ┐
    {"fetch_orders"},    // ├─> 这些并行运行
    {"fetch_products"},  // ┘
    {"rank", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

#### 图复用

使用不同的上下文多次执行同一个图：

```cpp
lark::Executor executor;

for (const auto& request : requests) {
  lark::DefaultContext ctx;
  lark::ProvideDomain<RequestDomain>(ctx, request.id, request.user_id);
  
  executor.Execute(*graph, ctx);  // 复用图结构
  
  process_result(ctx);
}
```

---

### 最佳实践

#### 1. 一个节点一个文件

```cpp
// fetch_user_node.cpp
class FetchUserNode : public lark::Node { ... };
LARK_NODE("fetch_user", FetchUserNode);
```

#### 2. 使用描述性名称

```cpp
LARK_NODE("fetch_user_profile", FetchUserProfileNode);  // ✓ 好
LARK_NODE("node1", FetchUserProfileNode);               // ✗ 差
```

#### 3. 优雅处理失败

```cpp
class ExternalServiceNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto result = call_service();  // 可能抛出异常
    lark::Set(ctx, "result", result);
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr) override {
    lark::Set(ctx, "result", DefaultValue{});  // 安全降级
    return true;
  }
};
```

#### 4. 使用类型安全的上下文

```cpp
// ✓ 好：类型安全
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<int>(ctx, "user_id");

// ✗ 差：类型不匹配
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<std::string>(ctx, "user_id");  // nullptr!
```

#### 5. 选择合适的线程池

```cpp
// CPU 密集型工作
co_await lark::Schedule(ctx, lark::PoolKind::kCompute);
process_data();

// I/O 工作
co_await lark::Schedule(ctx, lark::PoolKind::kIo);
fetch_from_network();

// 后台工作
co_await lark::Schedule(ctx, lark::PoolKind::kBackground);
log_metrics();
```

---

### 常见模式

#### 模式 1：扇出/扇入

```cpp
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"fetch_products"},
    {"aggregate", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

#### 模式 2：管道

```cpp
auto graph = builder.Build({
    {"extract"},
    {"transform", {"extract"}},
    {"load", {"transform"}},
});
```

#### 模式 3：条件分支

```cpp
auto graph = builder.Build({
    {"check_condition"},
    {"path_a", {"check_condition"}},
    {"path_b", {"check_condition"}},
    {"merge", {"path_a", "path_b"}},
});
```

#### 模式 4：重试带降级

```cpp
class RetryNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    for (int i = 0; i < 3; ++i) {
      try {
        auto result = risky_operation();
        lark::Set(ctx, "result", result);
        return;
      } catch (...) {
        if (i == 2) throw;  // 最后一次尝试
      }
    }
  }
  
  bool Fallback(lark::IContext& ctx, std::exception_ptr) override {
    lark::Set(ctx, "result", DefaultValue{});
    return true;
  }
};
```

---

<div align="center">

**Happy coding with LARK! 🚀**

[返回 README](README.md)

</div>

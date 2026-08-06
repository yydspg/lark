# LARK Usage Guide

<div align="center">


</div>

---

## 📘 Table of Contents

1. [Installation](#installation)
2. [Basic Concepts](#basic-concepts)
3. [Creating Nodes](#creating-nodes)
4. [Building Graphs](#building-graphs)
5. [Executing Graphs](#executing-graphs)
6. [Advanced Features](#advanced-features)
7. [Best Practices](#best-practices)
8. [Common Patterns](#common-patterns)

---

## Installation

### CMake Integration

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

### Manual Build

```bash
git clone https://github.com/yydspg/lark.git
cd lark
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
sudo cmake --install build
```

---

## Basic Concepts

### Node

A **Node** represents a unit of work in your DAG. Each node can:
- Read data from the context
- Write data to the context
- Depend on other nodes
- Handle failures with fallback logic

### Context

The **Context** is a type-safe data bag shared by all nodes. It supports:
- **Keyed data**: `Set(ctx, "key", value)` / `Get<T>(ctx, "key")` returns `T&`
- **Domain contexts**: `ProvideDomain<T>(ctx, args...)` / `Domain<T>(ctx)` returns `T&`
- **Existence check**: `Has<T>(ctx, "key")` returns `bool`
- **Required access**: `Require<T>(ctx, "key")` / `RequireDomain<T>(ctx)` throws if missing

### Graph

A **Graph** is a validated DAG of nodes. It ensures:
- No circular dependencies
- All dependencies are satisfied
- Topological execution order

### Executor

The **Executor** runs the graph using three thread pools:
- **Compute pool**: CPU-bound tasks
- **IO pool**: Network/disk I/O tasks
- **Background pool**: Low-priority tasks (logging, metrics)

---

## Creating Nodes

### Simple Synchronous Node

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

### Async Node with Pool Hopping

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

### Node with Fallback

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

## Building Graphs

### Basic Graph

```cpp
lark::GraphBuilder builder;
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"rank", {"fetch_user", "fetch_orders"}},
});
```

### Complex Graph with IDs

```cpp
auto graph = builder.Build({
    {"fetch_user", {}, "user_v1"},
    {"fetch_user", {}, "user_v2"},  // same type, different id
    {"aggregate", {"user_v1", "user_v2"}},
});
```

### Graph Validation

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

## Executing Graphs

### Basic Execution

```cpp
lark::DefaultContext ctx;
lark::Executor executor;
executor.Execute(*graph, ctx);

// Read results
auto result = lark::Get<int>(ctx, "result");
```

### Custom Pool Sizes

```cpp
lark::Executor executor(
    4,   // compute threads
    8,   // IO threads
    2    // background threads
);
executor.Execute(*graph, ctx);
```

### Adding Monitoring

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

## Advanced Features

### Domain Contexts

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

### Parallel Execution

Nodes with no dependencies run in parallel automatically:

```cpp
auto graph = builder.Build({
    {"fetch_user"},      // ┐
    {"fetch_orders"},    // ├─> These run in parallel
    {"fetch_products"},  // ┘
    {"rank", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

### Graph Reuse

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

## Best Practices

### 1. One Node Per File

```cpp
// fetch_user_node.cpp
class FetchUserNode : public lark::Node { ... };
LARK_NODE("fetch_user", FetchUserNode);
```

### 2. Use Descriptive Names

```cpp
LARK_NODE("fetch_user_profile", FetchUserProfileNode);  // ✓ Good
LARK_NODE("node1", FetchUserProfileNode);               // ✗ Bad
```

### 3. Handle Failures Gracefully

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

### 4. Use Type-Safe Context

```cpp
// ✓ Good: Type-safe
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<int>(ctx, "user_id");

// ✗ Bad: Type mismatch
lark::Set(ctx, "user_id", 42);
auto user_id = lark::Get<std::string>(ctx, "user_id");  // nullptr!
```

### 5. Choose the Right Pool

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

## Common Patterns

### Pattern 1: Fan-Out / Fan-In

```cpp
auto graph = builder.Build({
    {"fetch_user"},
    {"fetch_orders"},
    {"fetch_products"},
    {"aggregate", {"fetch_user", "fetch_orders", "fetch_products"}},
});
```

### Pattern 2: Pipeline

```cpp
auto graph = builder.Build({
    {"extract"},
    {"transform", {"extract"}},
    {"load", {"transform"}},
});
```

### Pattern 3: Conditional Branch

```cpp
auto graph = builder.Build({
    {"check_condition"},
    {"path_a", {"check_condition"}},
    {"path_b", {"check_condition"}},
    {"merge", {"path_a", "path_b"}},
});
```

### Pattern 4: Retry with Fallback

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

<div align="center">


**Happy coding with LARK! 🚀**

</div>


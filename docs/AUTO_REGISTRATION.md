# LARK Node Auto-Registration Guide

## Overview

LARK provides a Spring-like auto-registration mechanism using the `LARK_NODE` macro. Nodes are automatically registered at program startup through C++ static initialization - no manual registration calls needed!

## Usage

### Basic Example

```cpp
#include "dag/dag.h"

// 1. Define your node
class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    // Your business logic here
    lark::Set(ctx, "user", UserProfile{"john", 3});
  }
};

// 2. Register it with a single macro (at namespace scope)
LARK_NODE("fetch_user", FetchUserNode);
```

That's it! The node is automatically registered when the program starts.

### Complete Example

```cpp
#include "dag/dag.h"

class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    std::this_thread::sleep_for(40ms);
    lark::Set(ctx, "user", UserProfile{"user-123", 3});
  }
};
LARK_NODE("fetch_user", FetchUserNode);  // Auto-registered!

class FetchOrdersNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    std::this_thread::sleep_for(40ms);
    lark::Set(ctx, "orders", Orders{{"o1", "o2", "o3"}});
  }
};
LARK_NODE("fetch_orders", FetchOrdersNode);  // Auto-registered!

class BuildFeaturesNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto user = lark::Get<UserProfile>(ctx, "user");
    auto orders = lark::Get<Orders>(ctx, "orders");
    Features f;
    f.score = (user ? user->level : 0) * 10.0 +
              (orders ? static_cast<double>(orders->ids.size()) : 0.0);
    lark::Set(ctx, "features", f);
  }
};
LARK_NODE("build_features", BuildFeaturesNode);  // Auto-registered!

int main() {
  // Build graph - nodes are automatically discovered!
  lark::GraphBuilder builder;
  auto graph = builder.Build({
      {"fetch_user"},
      {"fetch_orders"},
      {"build_features", {"fetch_user", "fetch_orders"}},
  });

  lark::DefaultContext ctx;
  lark::Executor executor;
  executor.Execute(*graph, ctx);

  return 0;
}
```

## How It Works

The `LARK_NODE` macro uses C++ static initialization to register nodes at program startup:

```cpp
#define LARK_NODE(NAME, CLASS)                                              \
  namespace {                                                               \
  [[maybe_unused]] const bool kLarkNodeRegistered_##CLASS = [] {           \
    ::lark::NodeRegistry::Instance().Register(                              \
        (NAME), [] { return std::make_unique<CLASS>(); });                 \
    return true;                                                            \
  }();                                                                      \
  }
```

When your `.cpp` file is compiled, the lambda runs during static initialization (before `main()`), registering the node factory in the global registry.

## Benefits

1. **Zero boilerplate**: No manual `RegisterAll()` functions needed
2. **Decoupled**: Each node file is self-contained
3. **Type-safe**: Compile-time checking of node types
4. **Modular**: Add/remove nodes by adding/removing `.cpp` files
5. **Spring-like**: Familiar annotation-style registration

## Best Practices

### 1. Place Registration in Node's .cpp File

```cpp
// fetch_user_node.cpp
class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override { ... }
};
LARK_NODE("fetch_user", FetchUserNode);  // Right after the class definition
```

### 2. Use Descriptive Names

```cpp
LARK_NODE("fetch_user", FetchUserNode);        // ✓ Good
LARK_NODE("node1", FetchUserNode);             // ✗ Unclear
```

### 3. One Node Per File (Recommended)

While not required, it's cleaner to have one node class per `.cpp` file:

```
src/nodes/
  fetch_user_node.cpp      # Contains FetchUserNode + LARK_NODE
  fetch_orders_node.cpp    # Contains FetchOrdersNode + LARK_NODE
  build_features_node.cpp  # Contains BuildFeaturesNode + LARK_NODE
```

### 4. Verify Registration

You can check which nodes are registered:

```cpp
auto types = lark::NodeRegistry::Instance().RegisteredTypes();
std::cout << "Registered nodes:\n";
for (const auto& type : types) {
  std::cout << "  - " << type << "\n";
}
```

### 5. Duplicate Name Detection

The framework **automatically checks for duplicate node names** at registration time. If you try to register two different node classes with the same name, the program will throw a `std::invalid_argument` exception at startup:

```cpp
// file1.cpp
LARK_NODE("fetch_user", FetchUserNode);  // ✓ OK

// file2.cpp
LARK_NODE("fetch_user", AnotherFetchUserNode);  // ✗ Throws at startup!
// Error: NodeRegistry: duplicate node type 'fetch_user'
```

**Why this matters:**
- Catches naming conflicts early (at program startup, not at runtime)
- Prevents accidental node overwrites
- Makes debugging easier with clear error messages

**How to fix:**
- Use unique, descriptive names for each node type
- Follow naming conventions: `fetch_user`, `fetch_orders`, `rank_user`, etc.
- If you need multiple instances, use node IDs (not type names):
  ```cpp
  // Same type, different instances
  builder.Build({
      {"fetch_user", {}, "user_v1"},
      {"fetch_user", {}, "user_v2"},  // Same type, different ID
  });
  ```

## Advanced: Custom Registry

For testing or multi-tenant scenarios, you can create isolated registries:

```cpp
void TestWithIsolatedRegistry() {
  lark::NodeRegistry test_registry;  // Not the global Instance()
  
  // Manually register for this test
  test_registry.Register("test_node", [] { 
    return std::make_unique<TestNode>(); 
  });
  
  lark::GraphBuilder builder(test_registry);
  auto graph = builder.Build({{"test_node"}});
  
  // ... test code
}
```

## Backward Compatibility

The old `DAG_REGISTER_NODE` macro still works:

```cpp
DAG_REGISTER_NODE("fetch_user", FetchUserNode);  // Still works!
```

But prefer `LARK_NODE` for new code.

## Comparison: Before vs After

### Before (Manual Registration)

```cpp
// In each node file
class FetchUserNode : public lark::Node { ... };

// In a central registration file
void RegisterAllNodes(lark::NodeRegistry& reg) {
  reg.Register("fetch_user", [] { return std::make_unique<FetchUserNode>(); });
  reg.Register("fetch_orders", [] { return std::make_unique<FetchOrdersNode>(); });
  // ... repeat for every node
}

int main() {
  lark::NodeRegistry registry;
  RegisterAllNodes(registry);  // Manual call required!
  // ...
}
```

### After (Auto-Registration)

```cpp
// In each node file
class FetchUserNode : public lark::Node { ... };
LARK_NODE("fetch_user", FetchUserNode);  // That's it!

int main() {
  // Nodes are automatically registered - no setup needed!
  lark::GraphBuilder builder;
  auto graph = builder.Build({{"fetch_user"}});
  // ...
}
```

## Troubleshooting

### Node Not Registered?

1. **Check the .cpp file is compiled**: The registration only happens if the `.cpp` file is linked into your executable.
   
2. **Verify macro placement**: `LARK_NODE` must be at namespace scope (not inside a function or class).

3. **Check for typos**: The name in `LARK_NODE("name", Class)` must match what you use in `NodeDef`.

### Duplicate Registration Error?

The framework **automatically detects duplicate node names** at registration time and throws an exception:

```cpp
LARK_NODE("fetch_user", FetchUserNode);
LARK_NODE("fetch_user", AnotherNode);  // ✗ Error: duplicate name!
```

**Error message:**
```
terminate called after throwing an instance of 'std::invalid_argument'
  what():  NodeRegistry: duplicate node type 'fetch_user'
```

**Common causes:**
1. Copy-pasting a node file and forgetting to change the name
2. Two developers accidentally using the same name
3. Accidentally using the macro twice on the same class

**Solution:**
- Each node type must have a **unique name**
- Use descriptive, specific names: `"fetch_user_profile"`, `"fetch_user_orders"`, etc.
- If you need multiple instances of the same node type, use **node IDs** instead:
  ```cpp
  // Same node type, different instances
  builder.Build({
      {"fetch_user", {}, "user_node_1"},
      {"fetch_user", {}, "user_node_2"},
  });
  ```

## Performance

- **Registration**: Happens once at program startup (static init), zero runtime overhead
- **Lookup**: O(1) hash map lookup in the registry
- **Memory**: One factory function per node type (typically 24-32 bytes)

## Thread Safety

The `NodeRegistry` is thread-safe. Multiple threads can call `Register()` and `Create()` concurrently.

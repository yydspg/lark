# Context API Changes - Reference-Based Access

## Summary

The `Get<T>()` and `Domain<T>()` functions now return **object references** (`T&`) instead of `shared_ptr<T>`, providing a more intuitive and direct API for accessing context data.

## Before (Pointer-Based)

```cpp
// Old API - returned shared_ptr
auto user = lark::Get<UserProfile>(ctx, "user");
if (user) {
  std::cout << user->name << "\n";  // Need to dereference
}

auto domain = lark::Domain<RequestDomain>(ctx);
if (domain) {
  std::cout << domain->request_id << "\n";  // Need to dereference
}
```

## After (Reference-Based)

```cpp
// New API - returns T&
if (lark::Has<UserProfile>(ctx, "user")) {
  const auto& user = lark::Get<UserProfile>(ctx, "user");
  std::cout << user.name << "\n";  // Direct access!
}

const auto& domain = lark::Domain<RequestDomain>(ctx);
std::cout << domain.request_id << "\n";  // Direct access!
```

## Key Changes

### 1. Get<T>() - Returns Reference

**Before:**
```cpp
template <typename T>
shared_ptr<T> Get(const IContext& ctx, const string& key);
```

**After:**
```cpp
template <typename T>
T& Get(const IContext& ctx, const string& key);
// Throws std::out_of_range if key doesn't exist
```

### 2. Domain<T>() - Returns Reference

**Before:**
```cpp
template <typename T>
shared_ptr<T> Domain(const IContext& ctx);
```

**After:**
```cpp
template <typename T>
T& Domain(const IContext& ctx);
// Throws std::out_of_range if domain doesn't exist
```

### 3. Safe Access Pattern

Use `Has<T>()` to check existence before accessing:

```cpp
// Safe access pattern
if (lark::Has<UserProfile>(ctx, "user")) {
  const auto& user = lark::Get<UserProfile>(ctx, "user");
  // Use user directly
}

// Or use Require for guaranteed access (throws if missing)
const auto& user = lark::Require<UserProfile>(ctx, "user");
```

## Migration Guide

### Pattern 1: Conditional Access

**Old:**
```cpp
auto user = lark::Get<UserProfile>(ctx, "user");
if (user) {
  process(user->name);
}
```

**New:**
```cpp
if (lark::Has<UserProfile>(ctx, "user")) {
  const auto& user = lark::Get<UserProfile>(ctx, "user");
  process(user.name);
}
```

### Pattern 2: Required Access

**Old:**
```cpp
auto user = lark::Get<UserProfile>(ctx, "user");
if (!user) {
  throw std::runtime_error("missing user");
}
process(user->name);
```

**New:**
```cpp
const auto& user = lark::Require<UserProfile>(ctx, "user");  // Throws if missing
process(user.name);
```

### Pattern 3: Domain Context

**Old:**
```cpp
auto domain = lark::Domain<RequestDomain>(ctx);
if (domain) {
  log(domain->request_id);
}
```

**New:**
```cpp
const auto& domain = lark::Domain<RequestDomain>(ctx);  // Throws if missing
log(domain.request_id);

// Or safely:
if (lark::HasDomain<RequestDomain>(ctx)) {  // Note: need to add this if needed
  const auto& domain = lark::Domain<RequestDomain>(ctx);
  log(domain.request_id);
}
```

## Benefits

1. **More Intuitive**: Direct object access without pointer indirection
2. **Cleaner Syntax**: Use `.` instead of `->`
3. **Better Performance**: No shared_ptr overhead on access
4. **Clearer Ownership**: References make it clear the context owns the data
5. **Safer**: Forces explicit existence checks or use of Require

## Breaking Changes

This is a **breaking API change**. Code using the old pointer-based API must be updated:

- `if (ptr)` → `if (Has<T>(ctx, key))`
- `ptr->member` → `ref.member`
- `*ptr` → `ref`

## Testing

All existing tests have been updated to use the new API. The test suite verifies:
- Reference-based access works correctly
- Exceptions are thrown for missing keys
- Type safety is maintained
- Domain contexts work as expected

Run tests with:
```bash
./build/tests/dag_tests
```

All 41 tests pass with the new API.

# Contributing to LARK

<div align="center">

[English](#english) | [中文](#中文)

</div>

---

<a id="english"></a>
## 🌟 English

Thank you for your interest in contributing to LARK! This document provides guidelines and information for contributors.

### Code of Conduct

By participating in this project, you agree to abide by the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). Please report unacceptable behavior to the maintainers.

### How to Contribute

#### Reporting Bugs

Before creating a bug report, please check existing issues to avoid duplicates. When creating a bug report, include:

- **Clear title** - Concise description of the issue
- **Steps to reproduce** - Detailed steps to reproduce the behavior
- **Expected behavior** - What you expected to happen
- **Actual behavior** - What actually happened
- **Environment** - OS, compiler version, CMake version
- **Code example** - Minimal reproducible example if possible

Use the GitHub issue template and label it as `bug`.

#### Suggesting Enhancements

Enhancement suggestions are welcome! Please include:

- **Use case** - Why do you need this feature?
- **Proposal** - How should it work?
- **Alternatives** - Other solutions you've considered
- **Additional context** - Any other relevant information

Use the GitHub issue template and label it as `enhancement`.

#### Pull Requests

1. **Fork the repository**
   ```bash
   git clone https://github.com/your-username/lark.git
   cd lark
   ```

2. **Create a branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make your changes**
   - Follow the existing code style
   - Add tests for new functionality
   - Ensure all tests pass
   - Update documentation as needed

4. **Commit your changes**
   ```bash
   git commit -m "Add: brief description of your changes"
   ```

   Use conventional commit format:
   - `Add:` for new features
   - `Fix:` for bug fixes
   - `Docs:` for documentation changes
   - `Refactor:` for code refactoring
   - `Test:` for adding tests
   - `Chore:` for maintenance tasks

5. **Push to your fork**
   ```bash
   git push origin feature/your-feature-name
   ```

6. **Open a Pull Request**
   - Provide a clear description of the changes
   - Link any related issues
   - Ensure CI checks pass

### Development Setup

#### Prerequisites

- C++20 compatible compiler (Clang 16+, GCC 11+)
- CMake 3.20+
- Git

#### Building from Source

```bash
# Clone the repository
git clone https://github.com/yydspg/lark.git
cd lark

# Configure
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build -j

# Run tests
./build/tests/dag_tests

# Run example
./build/examples/simple_pipeline
```

#### Building with Sanitizers

```bash
# AddressSanitizer + UBSan
cmake -S . -B build-asan -DCMAKE_CXX_COMPILER=clang++ -DDAG_ENABLE_ASAN=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/dag_tests
```

### Code Style Guidelines

#### General Principles

- **Readability** - Code should be clear and self-documenting
- **Consistency** - Follow existing patterns in the codebase
- **Simplicity** - Prefer simple, straightforward solutions

#### Naming Conventions

- **Classes/Structs**: `PascalCase` (e.g., `NodeRegistry`, `DefaultContext`)
- **Functions/Methods**: `PascalCase` (e.g., `Create`, `GetPool`)
- **Variables**: `snake_case` (e.g., `node_id`, `pool_size`)
- **Constants**: `kPascalCase` (e.g., `kCompute`, `kIo`)
- **Namespaces**: `lowercase` (e.g., `lark`, `lark::coro`)

#### File Organization

- **Headers**: Declaration only, implementations in `.cpp` files
- **Exception**: Templates, inline functions, and coroutine types must be in headers
- **Include guards**: Use `#pragma once`
- **Include order**: 
  1. Related header
  2. C++ standard library headers
  3. Other project headers

#### Comments

- Use `//` for single-line comments
- Use `///` for documentation comments (future Doxygen support)
- Explain **why**, not **what**
- Keep comments up-to-date with code changes

#### Error Handling

- Use exceptions for exceptional situations
- Provide clear error messages
- Document which functions can throw

### Testing

- **Unit tests**: Required for all new functionality
- **Integration tests**: Required for features that span multiple components
- **Coverage**: Aim for >90% code coverage
- Run tests before submitting PR

```bash
# Run all tests
./build/tests/dag_tests

# Run specific test (if supported)
./build/tests/dag_tests --test=TestName
```

### Documentation

- Update README.md for user-facing changes
- Update USAGE.md for usage examples
- Add inline comments for complex logic
- Keep documentation in sync with code changes

### License

By contributing to LARK, you agree that your contributions will be licensed under the MIT License.

---

<a id="中文"></a>
## 🌟 中文

感谢您对贡献 LARK 的兴趣！本文档为贡献者提供指南和信息。

### 行为准则

参与本项目即表示您同意遵守[贡献者公约行为准则](CODE_OF_CONDUCT.md)。请将不可接受的行为报告给维护者。

### 如何贡献

#### 报告 Bug

在创建 Bug 报告之前，请先检查现有问题以避免重复。创建 Bug 报告时，请包括：

- **清晰的标题** - 问题的简洁描述
- **重现步骤** - 重现行为的详细步骤
- **预期行为** - 您期望发生什么
- **实际行为** - 实际发生了什么
- **环境信息** - 操作系统、编译器版本、CMake 版本
- **代码示例** - 如果可能，提供最小可重现示例

使用 GitHub issue 模板并标记为 `bug`。

#### 建议增强

欢迎提出增强建议！请包括：

- **使用场景** - 为什么需要这个功能？
- **提案** - 它应该如何工作？
- **替代方案** - 您考虑过的其他解决方案
- **附加上下文** - 任何其他相关信息

使用 GitHub issue 模板并标记为 `enhancement`。

#### Pull Request

1. **Fork 仓库**
   ```bash
   git clone https://github.com/your-username/lark.git
   cd lark
   ```

2. **创建分支**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **进行修改**
   - 遵循现有代码风格
   - 为新功能添加测试
   - 确保所有测试通过
   - 根据需要更新文档

4. **提交更改**
   ```bash
   git commit -m "Add: 您的更改的简要描述"
   ```

   使用约定式提交格式：
   - `Add:` 新功能
   - `Fix:` Bug 修复
   - `Docs:` 文档更改
   - `Refactor:` 代码重构
   - `Test:` 添加测试
   - `Chore:` 维护任务

5. **推送到您的 fork**
   ```bash
   git push origin feature/your-feature-name
   ```

6. **开启 Pull Request**
   - 提供清晰的更改描述
   - 链接任何相关问题
   - 确保 CI 检查通过

### 开发环境设置

#### 先决条件

- 支持 C++20 的编译器（Clang 16+、GCC 11+）
- CMake 3.20+
- Git

#### 从源码构建

```bash
# 克隆仓库
git clone https://github.com/yydspg/lark.git
cd lark

# 配置
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++

# 构建
cmake --build build -j

# 运行测试
./build/tests/dag_tests

# 运行示例
./build/examples/simple_pipeline
```

#### 使用 Sanitizer 构建

```bash
# AddressSanitizer + UBSan
cmake -S . -B build-asan -DCMAKE_CXX_COMPILER=clang++ -DDAG_ENABLE_ASAN=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/dag_tests
```

### 代码风格指南

#### 一般原则

- **可读性** - 代码应该清晰且自文档化
- **一致性** - 遵循代码库中的现有模式
- **简洁性** - 优先选择简单、直接的解决方案

#### 命名约定

- **类/结构体**：`PascalCase`（例如，`NodeRegistry`、`DefaultContext`）
- **函数/方法**：`PascalCase`（例如，`Create`、`GetPool`）
- **变量**：`snake_case`（例如，`node_id`、`pool_size`）
- **常量**：`kPascalCase`（例如，`kCompute`、`kIo`）
- **命名空间**：`lowercase`（例如，`lark`、`lark::coro`）

#### 文件组织

- **头文件**：仅声明，实现在 `.cpp` 文件中
- **例外**：模板、内联函数和协程类型必须在头文件中
- **包含保护**：使用 `#pragma once`
- **包含顺序**：
  1. 相关头文件
  2. C++ 标准库头文件
  3. 其他项目头文件

#### 注释

- 使用 `//` 进行单行注释
- 使用 `///` 进行文档注释（未来的 Doxygen 支持）
- 解释**为什么**，而不是**做什么**
- 保持注释与代码更改同步

#### 错误处理

- 对异常情况使用异常
- 提供清晰的错误消息
- 记录哪些函数可能抛出异常

### 测试

- **单元测试**：所有新功能都需要
- **集成测试**：跨多个组件的功能需要
- **覆盖率**：目标是 >90% 代码覆盖率
- 提交 PR 前运行测试

```bash
# 运行所有测试
./build/tests/dag_tests

# 运行特定测试（如果支持）
./build/tests/dag_tests --test=TestName
```

### 文档

- 更新面向用户的更改的 README.md
- 更新使用示例的 USAGE.md
- 为复杂逻辑添加内联注释
- 保持文档与代码更改同步

### 许可证

通过贡献 LARK，您同意您的贡献将根据 MIT 许可证进行许可。

---

<div align="center">

**Thank you for contributing! 🙏**

[Back to README](README.md)

</div>

# Contributing to LARK

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

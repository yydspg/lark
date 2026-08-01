# LARK Documentation Structure

## 📁 Documentation Files

### Main Entry Point
- **README.md** - Language selection hub (Bilingual)
  - Links to English and Chinese versions
  - Quick navigation table

### README Files (Project Documentation)
- **README_EN.md** - Complete English documentation
  - Overview & Features
  - Architecture diagrams
  - Core components
  - Quick start guide
  - Build instructions
  - License information

- **README_ZH.md** - Complete Chinese documentation (中文)
  - 概述与特性
  - 架构设计图
  - 核心组件
  - 快速开始
  - 构建说明
  - 许可证信息

### Usage Guides (Detailed Usage)
- **USAGE_EN.md** - English usage guide
  - Installation
  - Basic concepts
  - Creating nodes
  - Building graphs
  - Executing graphs
  - Advanced features
  - Best practices
  - Common patterns

- **USAGE_ZH.md** - Chinese usage guide (中文使用指南)
  - 安装
  - 基本概念
  - 创建节点
  - 构建图
  - 执行图
  - 高级特性
  - 最佳实践
  - 常见模式

### Contributing & License
- **CONTRIBUTING.md** - Contribution guidelines (Bilingual)
  - How to report bugs
  - How to submit PRs
  - Code style guide
  - Testing requirements

- **LICENSE** - MIT License
- **NOTICE** - License notice and guidelines

### Specialized Guides
- **docs/AUTO_REGISTRATION.md** - Node auto-registration guide (Bilingual)
  - LARK_NODE macro usage
  - How it works
  - Best practices
  - Troubleshooting

## 🗺️ Navigation Flow

```
README.md (Main Hub)
    ├─→ README_EN.md (English Project Docs)
    │   └─→ USAGE_EN.md (English Usage Guide)
    │
    └─→ README_ZH.md (中文项目文档)
        └─→ USAGE_ZH.md (中文使用指南)
```

## 📊 File Sizes

```
README.md        1.5K  (Language selector)
README_EN.md    10K    (English project docs)
README_ZH.md    9.8K   (Chinese project docs)
USAGE_EN.md     8.2K   (English usage guide)
USAGE_ZH.md     8.0K   (Chinese usage guide)
CONTRIBUTING.md 9.0K   (Bilingual contribution guide)
```

## 🌐 Language Support

| Document | English | Chinese | Notes |
|----------|---------|---------|-------|
| README.md | ✓ | ✓ | Main entry point |
| README_EN.md | ✓ | - | English only |
| README_ZH.md | - | ✓ | Chinese only |
| USAGE_EN.md | ✓ | - | English only |
| USAGE_ZH.md | - | ✓ | Chinese only |
| CONTRIBUTING.md | ✓ | ✓ | Bilingual in one file |
| AUTO_REGISTRATION.md | ✓ | ✓ | Bilingual in one file |

## �� Naming Convention

- `*_EN.md` - English-only documents
- `*_ZH.md` - Chinese-only documents
- `*.md` (no suffix) - Bilingual or language selector

## 🔗 Cross-References

All documents include navigation links:
- Back to main README
- Switch language version
- Related documents
- Table of contents

---

**Last updated**: 2024
**Maintained by**: LARK team

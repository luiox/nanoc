# AGENTS.md - NanoC Development Guide

## Project Overview

NanoC is a simple C-like language compiler that runs on a custom virtual machine. The project consists of:
- **ncc/** - NanoC compiler (lexer, parser, AST, codegen)
- **nvm/** - Virtual machine runtime
- **nas/** - Assembler
- **tests/** - GoogleTest-based test suite
- **examples/** - Sample NanoC programs

---

## Build System (Xmake)

### Prerequisites
- Xmake v3.0.0+
- C++17 compatible compiler (MSVC, GCC, Clang)
- Dependencies: gtest, spdlog (auto-installed by xmake)

### Build Commands

```bash
# Configure project (first time only)
xmake config

# Build all targets
xmake build

# Build specific target
xmake build ncc        # Compiler
xmake build nvm        # Virtual machine
xmake build nas        # Assembler
xmake build tests      # Test suite
xmake build compile_examples  # Example compiler runner

# Run targets
xmake run ncc
xmake run nvm
xmake run nas
xmake run tests
xmake run compile_examples

# Run tests (GoogleTest)
xmake test

# Clean build artifacts
xmake clean

# Rebuild from scratch
xmake clean && xmake build
```

### Running Single Tests

The test binary uses GoogleTest. To run specific tests:

```bash
# Build tests target
xmake build tests

# Run all tests
xmake run tests

# Run specific test suite (filter by name)
xmake run tests --gtest_filter=LexerTest.*
xmake run tests --gtest_filter=ParserTest.*
xmake run tests --gtest_filter=VMTest.*

# Run single test
xmake run tests --gtest_filter=LexerTest.Keywords

# List available tests
xmake run tests --gtest_list_tests
```

### Debug Build

```bash
# Configure for debug mode (default)
xmake f -m debug

# Configure for release mode
xmake f -m release
```

---

## Code Style Guidelines

### Formatting

The project uses **`.clang-format`** (GNU-based, 90 column limit). Format code before committing:

```bash
# Format all source files
xmake format
```

**Key formatting rules:**
- **Indentation**: 4 spaces (no tabs)
- **Column limit**: 90 characters
- **Brace style**: two styles coexist **by directory**, each with its own `.clang-format`
  - Root config (GNU): `nvm/`, `nas/` — return type & function brace on own line
  - `ncc/`, `tests/`, `examples/` have a K&R sub-config — function brace on same line
  - Never unify the two in one config; run the formatter per directory (it picks the nearest config automatically)
- **Namespace indentation**: Indented
- **Pointer alignment**: root = Middle (`int * ptr`); `ncc`/`tests`/`examples` = Left (`int* ptr`)
- **Include sorting**: Enabled (project/quoted first, then angle brackets alphabetically)
- **clang-format ≥16**: legacy keys such as `AlwaysBreakAfterReturnType` are silently overridden by `BasedOnStyle`; both keys are pinned explicitly in the root config — copy that pattern when adding style keys

### Header Guards

Use **`#ifndef`/`#define`/`#endif`** guards with descriptive names:

```cpp
#ifndef NCC_LEXER_H
#define NCC_LEXER_H

// ... content ...

#endif // NCC_LEXER_H
```

### Naming Conventions

**Classes**: PascalCase with `N` prefix for namespace disambiguation
```cpp
class Lexer;
class NVirtualMachine;
class NTokenKind;
```

**Files**: lowercase with underscores, match class/functionality name
```
lexer.hpp, lexer.cpp
codegen.hpp, codegen.cpp
```

**Variables**: camelCase with `m_` prefix for member variables
```cpp
std::string m_source;
int m_pos;
int32_t m_pc, m_sp, m_bp;
```

**Functions**: camelCase
```cpp
Token nextToken();
std::vector<Token> tokenize();
void advance();
```

**Constants/Enums**: SCREAMING_SNAKE_CASE or PascalCase for enum values
```cpp
constexpr int32_t DEFAULT_STACK_SIZE = 8*1024*1024;
enum class NTokenKind {
    KEYWORD_INT,
    KEYWORD_IF,
    // ...
};
```

### Include Order

Fully defined by `.clang-format` (`IncludeCategories`). The effective rule in every directory:

1. Project headers, quoted (e.g. `"lexer.hpp"`, `"../ncc/ast.hpp"`)
2. Angle-bracket headers, alphabetical (e.g. `<fstream>`, `<gtest/gtest.h>`, `<string>`)

Don't hand-sort includes — run `clang-format` on the files you touch.

### Error Handling

**Use exceptions for compiler errors:**
```cpp
try {
    auto program = parser.parse();
} catch (const std::exception& e) {
    std::cerr << "Compilation error: " << e.what() << std::endl;
    return false;
}
```

**Use spdlog for logging:**
```cpp
#include <spdlog/spdlog.h>

spdlog::info("Compilation started");
spdlog::error("Error message with arg: {}", value);
spdlog::debug("Debug info");
```

**Test assertions with GoogleTest:**
```cpp
TEST(LexerTest, Keywords) {
    ASSERT_EQ(tokens.size(), 11);
    EXPECT_EQ(tokens[0].kind, NTokenKind::KEYWORD_INT);
    EXPECT_TRUE(result != nullptr);
}
```

### C/C++ Interop

The project mixes C (`main.c`) and C++ (`.cpp`) files:
- Use `extern "C"` guards in headers exposed to C files
- Avoid C++ features in `.c` files
- Keep C code minimal (main entry points only)

### Windows Compatibility

- Avoid Windows macro conflicts (e.g., use `NTokenKind` instead of `TokenKind`)
- Use `#ifdef type / #undef type` guards if conflicts occur
- Save files with **UTF-8 encoding** to prevent C4819 warnings

---

## Project Structure

```
NanoC/
├── ncc/           # Compiler source
│   ├── lexer.hpp/.cpp      # Tokenizer
│   ├── parser.hpp/.cpp     # AST parser
│   ├── ast.hpp/.cpp        # AST node definitions
│   ├── codegen.hpp/.cpp    # Code generation
│   └── main.c              # Compiler entry point
├── nvm/           # Virtual machine
│   ├── core.hpp/.cpp       # VM implementation
│   ├── instructions.hpp/.cpp  # Instruction set
│   ├── string_helper.hpp   # Utility
│   └── main.cpp            # VM runner
├── nas/           # Assembler
│   └── main.cpp            # Assembler
├── tests/         # GoogleTest tests
│   ├── test_main.cpp       # Test runner
│   ├── test_lexer.cpp
│   ├── test_parser.cpp
│   ├── test_codegen.cpp
│   ├── test_vm.cpp
│   └── test_instructions.cpp
├── examples/      # Sample .nc programs
├── test/          # Legacy test files (.nas, .nca)
├── xmake.lua      # Build configuration
├── .clang-format  # Code formatting rules
└── README.md
```

---

## Testing Guidelines

### Writing Tests

1. **One test class per component**: `TEST(LexerTest, ...)` for lexer, etc.
2. **Use descriptive test names**: `Keywords`, `BasicAssertion`, `SpdlogTest`
3. **Initialize logging in tests**:
```cpp
TEST(SpdlogTest, BasicLogging) {
    spdlog::info("Test message");
    EXPECT_TRUE(true);
}
```

4. **Use ASSERT_ for fatal conditions, EXPECT_ for non-fatal**:
```cpp
ASSERT_EQ(tokens.size(), 11);  // Test cannot continue if wrong
EXPECT_EQ(token.kind, expected);  // Log failure but continue
```

### Test Organization

- `test_main.cpp`: GTest and spdlog initialization
- `test_*.cpp`: One per component under test
- Include source files directly in `xmake.lua` for test target (no separate compilation)

---

## Development Workflow

1. **Make changes** to source files
2. **Format code**: `xmake format`
3. **Build tests**: `xmake build tests`
4. **Run tests**: `xmake run tests` (or use filter for specific tests)
5. **Build project**: `xmake build`
6. **Test manually**: Compile a `.nc` file and run on VM

### Common Issues

**C4819 Warning (Unicode)**: Save files as UTF-8 with BOM
```bash
# In VSCode: File → Save with Encoding → UTF-8 with BOM
```

**gtest not found**: Run `xmake config` to install dependencies
```bash
xmake config
xmake
```

**Link errors**: Clean and rebuild
```bash
xmake clean && xmake config && xmake build
```

---

## Cursor/Copilot Rules

No `.cursorrules`, `.cursor/rules/`, or `.github/copilot-instructions.md` files exist in this repository.

---

## Quick Reference

| Task | Command |
|------|---------|
| Build all | `xmake` |
| Build compiler | `xmake build ncc` |
| Run tests | `xmake run tests` |
| Run single test | `xmake run tests --gtest_filter=TestName.*` |
| Format code | `xmake format` |
| Clean build | `xmake clean` |
| Release build | `xmake f -m release && xmake` |
| Debug build | `xmake f -m debug && xmake` |

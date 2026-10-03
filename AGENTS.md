# AGENTS.md - NanoC Development Guide

## Project Overview

NanoC is a simple C-like language with one shared frontend/IR and three backends
(NAS/VM, C, LLVM). The project consists of:
- **ncc/** - NanoC compiler (lexer, parser, AST, semantic, IR, preprocessor, NAS/C/LLVM backends, multi-file loading)
- **nvm/** - Virtual machine runtime (loader, host-function dispatch, `--host-lib` dynamic linking)
- **nas/** - Assembler + NCI v2.1 linker (`nas -r`)
- **rules/nanoc/** - xmake `rule("nanoc")` + `toolchain("nanoc")` + `ncc-separate` driver for building `.nc` sources
- **tests/** - GoogleTest-based test suite (incl. R13 differential matrix, vm/c/llvm)
- **examples/** - Sample NanoC programs + hello_project / toolchain_project xmake samples

---

## Build System (Xmake)

### Prerequisites
- Xmake v3.0.0+
- C++17 compatible compiler (MSVC, GCC, Clang)
- Dependencies: gtest, spdlog, libca (auto-installed by xmake)
- Optional: LLVM toolchain (`llc` + lld-link/clang) for the `--emit=obj|exe`
  backends and the llvm diff column — probe order `NANOC_LLVM_DIR` > `llc` on
  PATH; the diff column SKIPs (not fails) when absent

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

1. Project headers, quoted **with module prefix** (e.g. `"ncc/lexer.hpp"`, `"nvm/core.hpp"`,
   `"nas/instruction.hpp"`)
2. Angle-bracket headers, alphabetical (e.g. `<fstream>`, `<gtest/gtest.h>`, `<string>`) —
   external libs only (gtest/spdlog/libca)

Include roots are the module `src` dirs (`ncc/src`, `nvm/src`, `nas/src`) plus the
`tests` dir itself (`xmake.lua` adds `tests` as include root), so every file —
including sibling files inside the same module — writes the full module-prefixed path
(e.g. `"ncc/lexer.hpp"`, `"support/diff_harness.hpp"` from `tests/`).
Never use bare filenames (`"lexer.hpp"`) or `../`-relative paths (`"../ncc/lexer.hpp"`).

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

All sources are C++17 today (entry points are `.cpp`; `ncc` links the C++ libca
library), but the C interop rules still apply if `.c` files are added:
- Use `extern "C"` guards in headers exposed to C files
- Avoid C++ features in `.c` files
- Keep C code minimal (entry points only)

### Windows Compatibility

- Avoid Windows macro conflicts (e.g., use `NTokenKind` instead of `TokenKind`)
- Use `#ifdef type / #undef type` guards if conflicts occur
- Save files as **UTF-8**（无 BOM 即可）：`xmake.lua` 已对 MSVC 全局加
  `/utf-8`（限定 `tools = cl`），源码/执行字符集均为 UTF-8，C4819 与
  "中文按 CP936 误读吞引号"的级联语法错误均已消除；不要把源码转成 GBK

### Bytecode Format (NCI v2.1)

- 文件布局、导入/导出表 entry 字节图、数据段统一编址、entryPoint 规则的
  **权威定义**：`doc/Bytecode Format Specification v2.1.md` §2/§2.1
- nas（汇编侧）与 nvm（加载侧）必须按同一张钉死字节图实现，改动布局须
  先改规范、同步两侧实现与测试（test_assembler_v21 / test_loader /
  test_integration_e2e 均为字节级断言）

---

## Project Structure

```
NanoC/
├── ncc/           # Compiler source — layout: <module>/src/<module>/ (module-prefixed includes)
│   └── src/ncc/
│       ├── lexer.hpp/.cpp      # Tokenizer（含 defer/match 关键字）
│       ├── parser.hpp/.cpp     # AST parser（多文件 import/export、extern 声明、defer/match、头文件模式）
│       ├── ast.hpp/.cpp        # AST node definitions
│       ├── semantic.hpp/.cpp   # 语义分析（作用域栈/类型检查/file:line:col 诊断）
│       ├── ir.hpp/.cpp         # IR 数据模型与 AST 降级器（非 SSA，可 --dump-ir；defer 展开/match 降解）
│       ├── codegen.hpp/.cpp    # NAS 后端：ir::Module → 文本汇编
│       ├── c_backend.hpp/.cpp  # C 后端：ir::Module → 可读 C（--emit=c）
│       ├── llvm_backend.hpp/.cpp # LLVM 后端：ir::Module → 文本 .ll；--emit=llvm|obj|exe（NANOC_LLVM_DIR 探测）
│       ├── preprocessor.hpp/.cpp # R9 #include 预处理（引号 include 递归展开、guard/对象宏识别）
│       ├── loader.hpp          # 多文件装载（import 闭包 + R9 头文件段合并；loadStandalone 独立编译轻装载，header-only）
│       └── main.cpp            # CLI entry point（libca opt：--emit=asm|c|llvm|obj|exe、-o、-MMD/-MF、--dump-ir）
├── nvm/           # Virtual machine
│   └── src/nvm/
│       ├── core.hpp/.cpp       # VM implementation（加载器/宿主分发/--host-lib 动态链接）
│       ├── instructions.hpp    # Instruction set
│       ├── string_helper.hpp   # Utility
│       ├── handlers/           # Per-instruction handler methods
│       └── main.cpp            # VM runner（--host-lib、--Xss）
├── nas/           # Assembler + linker
│   └── src/nas/
│       ├── instruction.hpp/.cpp  # Instruction parsing & encoding
│       ├── linker.hpp/.cpp       # NCI v2.1 链接器（nas -r：段合并/重定位/符号解析）
│       └── main.cpp              # Assembler entry（-r 链接模式）
├── rules/nanoc/   # xmake 接入：nanoc.lua（rule 一期形态）+ toolchain.lua（toolchain 正式形态）
│   └── driver/                    # ncc-separate 独立编译驱动（R7 loadStandalone 的 CLI 接线）
├── tests/         # GoogleTest tests（include root = tests/，support/ 头按模块前缀引用）
│   ├── test_main.cpp            # Test runner
│   ├── support/                 # 差分测试共用 harness（diff_harness.hpp/.cpp）
│   ├── test_lexer.cpp / test_parser.cpp / test_semantic.cpp
│   ├── test_ir.cpp / test_ir_codegen.cpp         # IR 模型与降级器
│   ├── test_codegen.cpp / test_codegen_bridge.cpp / test_codegen_e2e.cpp
│   ├── test_c_backend.cpp       # C 后端 emit 与 C/VM 差分
│   ├── test_llvm_backend.cpp    # LLVM 后端黄金片段与真编译差分
│   ├── test_extern.cpp / test_multifile.cpp      # extern 声明（含 msvcrt 真宿主 e2e）/ 多文件
│   ├── test_defer_match.cpp     # M5 defer/match（语义/IR/VM/三后端差分）
│   ├── test_coro.cpp            # M6 协程 coro/yield（语义/IR 变换/e2e/三后端差分）
│   ├── test_include_headers.cpp # M7 #include 头文件（正例/负例/语义/e2e 差分）
│   ├── test_separate.cpp        # R7 独立编译 loadStandalone
│   ├── test_vm.cpp              # VM 执行级用例
│   ├── test_instructions.cpp    # 指令编码断言（Assembler::parseLine）
│   ├── test_assembler.cpp / test_assembler_v21.cpp  # 汇编器与 v2.1 目标格式字节级用例
│   ├── test_linker.cpp          # nas -r 链接器用例
│   ├── test_loader.cpp          # v2.1 加载器/宿主分发/动态链接用例
│   ├── test_hostlib.cpp         # --host-lib 宿主库用例
│   ├── test_golden_e2e.cpp      # examples 全工具链黄金 e2e
│   ├── test_integration_e2e.cpp # 汇编→加载→宿主调用全链路 e2e
│   ├── test_diff_matrix.cpp     # R13 差分矩阵（程序 × vm/c/llvm 后端）
│   └── test_libca.cpp           # libca smoke test
├── examples/      # Sample .nc programs（含 coro_iterator.nc；hello_project/ rule 样例、toolchain_project/ toolchain 样例）
├── test/          # Legacy test files (.nas, .nca)
├── doc/           # 设计文档与规范（NCI v2.1 权威规范、PRD 多后端路线图、xmake rule/toolchain 指南、开发计划）
├── .github/workflows/ci.yml  # CI：windows-latest + xmake 构建 + 全量测试（含 LLVM 工具链安装与 llvm 差分列）
├── xmake.lua      # Build configuration（含 MSVC /utf-8 全局标志）
├── .clang-format  # Formatting (root=GNU for nvm/nas; ncc/tests/examples=K&R sub-configs)
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
- `tests/support/`: Shared harness for the differential matrix (`diff_harness.hpp/.cpp`)
- Include source files directly in `xmake.lua` for test target (no separate compilation)

### Differential Testing (R13 Matrix)

- `test_diff_matrix.cpp` + `tests/support/diff_harness.hpp/.cpp` run a
  **program set × backend matrix** (examples + feature cases × vm/c/llvm) asserting the
  same program produces the same observable exit code on every available backend.
  The LLVM column uses the same `IDiffBackend` interface (probe `NANOC_LLVM_DIR`
  > `llc` on PATH; `.ll` → `llc -filetype=obj` → lld-link/clang link).
- Mapping convention: VM main return = R0 (int32_t); the C backend's exit code is
  compared as `& 0xFF`. Keep matrix-program `main` return values in `[0, 255]` so the
  mapping stays injective (255-truncation boundary).
- Anchor assertions: known programs additionally assert the raw VM R0, so the
  backends cannot be "consistently wrong".
- **Skip policy**: backend availability is probed at runtime — the C compiler probe is
  `NANOC_C_COMPILER` env var > `clang` on PATH > `gcc` on PATH; when fewer than 2
  executable backends exist the row is `GTEST_SKIP` (marked skip, not failure).
- Feature rows that need their own programs (e.g. defer/match in
  `test_defer_match.cpp`) reuse the same harness via `runRow` instead of adding
  matrix columns.
- When adding a language feature, add its .nc case to the matrix (and to
  `test_golden_e2e.cpp` anchors when applicable).

---

## Development Workflow

1. **Make changes** to source files
2. **Format code**: `xmake format`
3. **Build tests**: `xmake build tests`
4. **Run tests**: `xmake run tests` (or use filter for specific tests)
5. **Build project**: `xmake build`
6. **Test manually**: Compile a `.nc` file and run on VM（多模块用 `nas -r` 链接；
   或 `--emit=c|llvm|obj|exe` 产出原生可执行文件）

### Common Issues

**C4819 / 中文编码问题**：`xmake.lua` 已全局加 `/utf-8`（MSVC），源码用
无 BOM 的 UTF-8 即可；若新建目标发现中文注释/字符串导致 C2001"常量中有
换行符"级联报错，检查该目标是否受全局标志覆盖（新标志需限定
`{tools = "cl"}`，勿影响 gcc/clang）

**clang-format 版本**：根 `.clang-format` 钉死了 v22 键
（`BreakAfterReturnType` 等），PATH 上的 v18 解析不了；用
`D:/sdk/python/Python314/Scripts/clang-format.exe`（22.1.5），或
`xmake format`

**gtest not found**: Run `xmake config` to install dependencies
```bash
xmake config
xmake
```

**Link errors**: Clean and rebuild
```bash
xmake clean && xmake config && xmake build
```

**git/GitHub 网络双栈**：直连与全局代理因网络环境切换互有可达性（同一条
push/`gh`/`curl api.github.com` 这次直连通、下次可能只有代理通）。失败时
**换栈重试即可**：直连失败挂代理（`export https_proxy=http://127.0.0.1:<代理端口>`，
端口以本机代理软件为准），代理失败则 `unset https_proxy http_proxy` 重试；
push 被中断重发是安全的。

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
| Compile to VM assembly | `xmake run ncc file.nc -o file.nas` |
| Assemble | `xmake run nas file.nas file.nci` |
| Link NCI objects | `xmake run nas -r a.nci b.nci -o app.nci` |
| Run on VM | `xmake run nvm file.nci [--host-lib <dll>]` |
| Native backends | `xmake run ncc file.nc --emit=c\|llvm\|obj\|exe` |
| Format code | `xmake format` |
| Clean build | `xmake clean` |
| Release build | `xmake f -m release && xmake` |
| Debug build | `xmake f -m debug && xmake` |

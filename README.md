# NanoC

[![CI](https://github.com/luiox/nanoc/actions/workflows/ci.yml/badge.svg)](https://github.com/luiox/nanoc/actions/workflows/ci.yml)

NanoC 是一个类似 C 语言子集的编译器 + 虚拟机项目，用于学习编译器与虚拟机的完整实现。当前已实现**一个共享前端与 IR、三个编译后端并存**的形态（多后端路线图见 [PRD](doc/PRD-多后端编译与语言特性.md)）。

## 工具链与三个后端

```
                 ┌─ B0 NAS 后端 ──> .nas ──(nas)──> .nci ──(nvm)──> VM 执行
.nc 源码 ──ncc──>├─ B1 C 后端   ──> .c ──(cl/clang/gcc)──> 原生 exe
（前端+IR）       └─ B2 LLVM 后端 ─> .ll ──(llc/lld-link)──> .obj / 原生 exe
```

| 组件 | 说明 |
|------|------|
| **ncc** | Nano C 编译器：词法/语法分析 → AST → 语义分析（作用域/类型检查）→ IR（`--dump-ir` 可查看）→ 后端发射；支持多文件模块（`import`/`export`）、`extern` 声明、`#include` C 头文件声明子集、`defer`/`match` 语言特性 |
| **nas** | 汇编器 + 链接器：汇编为 NCI v2.1 二进制（32 字节头 + 代码段 + 数据段 + 导入/导出表）；`nas -r` 多目标链接（段合并/重定位/符号解析） |
| **nvm** | 虚拟机：48 条极简指令，无系统调用；CALLX 调用宿主 C 函数（静态注册、`--host-lib` 动态链接、链接器内部解析） |
| **C 后端** | `ncc --emit=c` 产出可读 C（`ir::Module` → C），交给系统 C 工具链编译为原生 exe，兼作差分测试 oracle |
| **LLVM 后端** | `ncc --emit=llvm|obj|exe`：`ir::Module` → 文本 .ll → llc（+ lld-link/clang 链接）产出 .obj / 原生 exe（工具链探测：`NANOC_LLVM_DIR` > PATH 上的 `llc`） |

## 快速上手

### VM 后端（NAS 链路）

```bash
# 1. 编译 .nc 源码为汇编
xmake run ncc examples/arithmetic.nc -o arithmetic.nas

# 2. 汇编为字节码
xmake run nas arithmetic.nas arithmetic.nci

# 3. 虚拟机执行
xmake run nvm arithmetic.nci
```

### extern 声明 + 宿主库（调用真实 C 函数）

NanoC 程序可用 `extern` 声明宿主提供的 C 函数（varargs 用 `...`，按 cdecl 发射）：

```c
// hello_ext.nc
extern int puts(char* s);

int main() {
    puts("hello from NanoC");
    return 0;
}
```

```bash
xmake run ncc hello_ext.nc -o hello_ext.nas
xmake run nas hello_ext.nas hello_ext.nci
# --host-lib 按符号名解析动态导入（可多次指定；已知签名白名单见规范 §2.1）
xmake run nvm hello_ext.nci --host-lib C:/Windows/System32/msvcrt.dll
# -> hello from NanoC
```

C/LLVM 后端的 `extern` 走 C 原生链接（系统库/`add_links` 解析，无需宿主库），`printf` 等 varargs 函数可直接调用；VM 后端的 varargs 宿主包装器仍在扩展中（当前白名单：`puts`/`putchar`/`abs`/`atoi`/`strlen`/`exit` 等）。

### 多目标链接（nas -r）

多个 NCI v2.1 目标文件（由 nas 汇编 `.nas` 产出，或独立编译产出）可用 `nas -r`
链接为单个可执行文件：段合并、地址重定位、跨模块导入/导出按符号名解析
（内部导出优先于宿主解析，entryPoint 优先取 `main` 导出）：

```bash
xmake run nas -r main.nci math.nci -o app.nci
xmake run nvm app.nci
```

链接后仍未解析的动态导入，运行期由 nvm 经 `--host-lib` 按符号名解析。

### C 后端（原生 exe）

```bash
xmake run ncc examples/arithmetic.nc --emit=c -o arithmetic.c
clang arithmetic.c -o arithmetic.exe   # 或 cl/gcc；运行结果与 VM 后端一致
```

### LLVM 后端（.ll / .obj / 原生 exe）

```bash
xmake run ncc examples/arithmetic.nc --emit=llvm -o arithmetic.ll  # 文本 LLVM IR
xmake run ncc examples/arithmetic.nc --emit=exe -o arithmetic.exe  # llc + lld-link/clang
./arithmetic.exe                        # 运行结果与 C/VM 后端一致
```

需 LLVM 工具链：设 `NANOC_LLVM_DIR`（指向 LLVM bin 或根目录），或让 `llc` 在
PATH 上；缺工具链时报错含安装指引。

### #include C 头文件（声明子集）

`#include "path.h"`（引号形式、相对当前文件）可把 C 头文件的声明子集并入
符号表，随后按签名直接调用（经 extern 发射路径）或访问 struct 成员：

```c
// mylib.h
#ifndef MYLIB_H
#define MYLIB_H
int add(int a, int b);
#endif
```

```c
// main.nc
#include "mylib.h"

int main() {
    return add(40, 2);
}
```

支持 `#pragma once`/`#ifndef` guard、对象宏与 enum 常量、`typedef`/`struct`/
`enum`、函数原型、指针/数组与 `const`/`unsigned` 等修饰符；函数宏与 `#if`
条件编译不支持（明确报错，不做误编译）。

### xmake rule 一条命令出 exe

用户工程引入 [`rule("nanoc")`](rules/nanoc/nanoc.lua) 即可把 `.nc` 当一等源文件构建
（`.nc → C → 系统 C 工具链`，增量缓存内置）。完整样例见
[examples/hello_project](examples/hello_project/)：

```lua
-- xmake.lua
includes("rules/nanoc/nanoc.lua")

target("hello")
    set_kind("binary")
    add_rules("nanoc")
    add_files("src/**.nc")
```

```console
$ xmake            # .nc → C → exe，一条命令
$ xmake run hello
```

### xmake toolchain 按文件独立编译

多模块、重增量的正式场景用 [`toolchain("nanoc")`](rules/nanoc/toolchain.lua)：
每个 `.nc` 经 `ncc-separate` 独立编译为各自的 `.c`/`.obj`，目标级用 C 链接器
链接，**改哪个文件只重编哪个文件**。完整样例见
[examples/toolchain_project](examples/toolchain_project/)：

```lua
-- xmake.lua
includes("<nanoc SDK>/rules/nanoc/toolchain.lua")       -- toolchain + 规则
includes("<nanoc SDK>/rules/nanoc/driver/xmake.lua")    -- 独立编译驱动

target("app")
    set_kind("binary")
    set_toolchains("nanoc")
    add_rules("nanoc.toolchain")
    add_deps("ncc-separate")
    add_files("src/**.nc")
```

rule 与 toolchain 两种形态的选择指南见
[doc/xmake-toolchain.md](doc/xmake-toolchain.md)（rule 见
[doc/xmake-rule.md](doc/xmake-rule.md)）。

## 构建

需要 [Xmake](https://xmake.io) 与 C++17 编译器（gtest/spdlog/libca 依赖自动安装）：

```bash
xmake            # 构建全部目标
xmake run tests  # 运行 GoogleTest 测试套件
```

常用命令与代码规范见 [AGENTS.md](AGENTS.md)。

## 目录结构

```
ncc/src/ncc/     编译器（lexer/parser/ast/semantic/ir/codegen/c_backend/llvm_backend/preprocessor/loader）
nas/src/nas/     汇编器与链接器（instruction：编码与汇编；linker：nas -r）
nvm/src/nvm/     虚拟机（core：加载器与执行；handlers/：分指令 handler）
rules/nanoc/     xmake rule("nanoc") + toolchain("nanoc")；driver/ 为 ncc-separate 独立编译驱动
tests/           GoogleTest 测试（support/ 为差分测试共用 harness）
examples/        示例 .nc 程序；hello_project（rule 样例）/ toolchain_project（toolchain 样例）
doc/             设计文档与规范
```

## 差分测试

`tests/test_diff_matrix.cpp`（PRD R13）把差分断言收敛为可扩展矩阵：
**15 个程序（examples 6 例 + 9 个特性用例）× 可用后端（vm/c/llvm）**，断言同一程序在
各后端的可观测退出码一致，并以已知 R0 锚点防止"一致地错"。后端可用性动态探测：
无 C 编译器（探针 `NANOC_C_COMPILER` > PATH clang > gcc）或无 LLVM 工具链
（`NANOC_LLVM_DIR` > PATH llc）时对应列记 SKIP 而非失败。defer/match 特性的
三后端一致性差分行复用同一 harness（`test_defer_match.cpp`）。

## 文档

| 文档 | 内容 |
|------|------|
| [doc/Bytecode Format Specification v2.1.md](doc/Bytecode%20Format%20Specification%20v2.1.md) | NCI v2.1 字节码格式权威规范（指令集、文件布局、链接语义、调用约定） |
| [doc/PRD-多后端编译与语言特性.md](doc/PRD-多后端编译与语言特性.md) | 产品路线图（R0-R14，多后端与语言特性；含里程碑进度对账） |
| [doc/开发计划 NCIv2.1.md](doc/开发计划%20NCIv2.1.md) | NCI v2.1 基座五阶段计划（已完成；后续演进以 PRD 为准） |
| [doc/xmake-rule.md](doc/xmake-rule.md) | rule("nanoc") 接入指南与已知边界 |
| [doc/xmake-toolchain.md](doc/xmake-toolchain.md) | toolchain("nanoc") 接入指南、与 rule 的选择指南 |
| [doc/Complier Design Description.md](doc/Complier%20Design%20Description.md) | 工具链流水线与文件格式总览 |
| [doc/需求设计文档.md](doc/需求设计文档.md) | 功能/非功能需求与验收标准 |

## 当前状态

- ✅ NCI v2.1 指令集（48 条）与 VM 执行层、栈帧管理、栈底哨兵
- ✅ nas 汇编器 v2.1 完整目标文件 + 链接器（`nas -r`：段合并/重定位/符号解析）
- ✅ VM 加载器（严格校验、导入/导出表、数据段载入、`--host-lib` 动态链接与签名包装器）
- ✅ ncc 前端：语义分析、类型系统（字符串/指针/数组/struct/typedef）、IR、多文件 import/export
- ✅ C 后端 `--emit=c`（原生 exe，差分 oracle）；extern 声明（C 后端真调 msvcrt，VM 侧 cdecl）
- ✅ LLVM 后端 `--emit=llvm|obj|exe`（三后端差分矩阵 vm/c/llvm）
- ✅ 独立编译 + 链接：`nas -r`、ncc `loadStandalone`、`ncc-separate` 驱动
- ✅ xmake 两种接入：`rule("nanoc")`（hello_project）+ `toolchain("nanoc")`（toolchain_project）
- ✅ 语言特性：`defer`（作用域退出逆序执行）、`match`（常量/区间/多值/守卫/通配）、`coro/yield`（无栈协程，IR 状态机变换，三后端一致）
- ✅ `#include` C 头文件声明子集（guard/对象宏/原型/struct/typedef/修饰符）
- ✅ M6 协程 coro/yield（PRD R12）：IR 状态机变换，`examples/coro_iterator.nc` 三后端一致
- ✅ 测试 500 项全绿（含黄金 e2e、链接器、宿主库、协程、三后端差分矩阵）；GitHub Actions CI（windows-latest + LLVM 差分列）

# NanoC PRD — 多后端编译与语言特性 v1.0

| 项 | 值 |
|---|---|
| 状态 | 已确认（两轮评审）；2026-10-03 终轮对账：M0–M5、M7 已交付，M6（coro）进行中 |
| 日期 | 2026-09-28 |
| 范围 | R0–R14 共 15 项需求，8 个里程碑 |
| 配套 | `doc/开发计划 NCIv2.1.md`（部分被本文取代，见附录 A）、`doc/Bytecode Format Specification v2.1.md`（继续有效） |

---

## 1 背景与目标

### 1.1 定位

NanoC 是一个教学向 C 子集语言，现有链路为「ncc → .nas 汇编文本 → nas → .nci 字节码 → nvm 执行」。本 PRD 将其升级为：

**一个共享前端与 IR、拥有三个后端（NAS/VM、C、LLVM）的编译器，支持多文件模块、可产出独立运行的原生可执行文件，并由 xmake 作为语言侧构建系统。**

### 1.2 目标

1. **三后端并存**：NAS/VM 后端（教学与差分基准）、C 后端（最快落地 + oracle）、LLVM 后端（正式原生产物）
2. **多文件模块机制**：文件即模块，`import`/`export`，先整体编译后独立编译+链接
3. **构建系统落地**：先 `rule("nanoc")`、后完整 `toolchain("nanoc")`，真实项目可 `add_files("src/**.nc")`
4. **语言特性**：`defer`、`match` 模式匹配、`coro` 无栈协程（状态机变换）
5. **外部库**：`extern` 声明（一期）→ `include` C 头文件声明子集（二期）
6. **实现基座**：编译器实现渐进式采用 libca（`C:\sw\libca`，27 模块 C++17 基础库）

### 1.3 一句话验收

> 一个真实的多文件 NanoC 项目，写 `import` + `extern`，在用户的 `xmake.lua` 里 `set_toolchains("nanoc")` 或 `add_files("src/**.nc")`，一条 `xmake` 命令产出可独立运行的 exe，且同一程序在三个后端上运行结果一致。

---

## 2 现状与差距

以下问题均已实地验证（2026-09-28，工作区 `main@c637956`，Windows/MSVC 2022）。

**2026-10-03 终轮对账：S1–S11 已全部解决**（解决 PR 逐项见状态列）。

| # | 问题 | 证据 | 影响 | 状态（2026-10-03） |
|---|---|---|---|---|
| S1 | ncc codegen 发**旧助记符**：`trap 2`、`jic`、`eq/ne/gt/ge/lt/le`、`st R0,[BP-8]`、`add SP,100` | `ncc/codegen.cpp:133,235,405,155,196` | 编译出的 .nas 无法被新汇编器（30 条 NCI v2.1 指令，`nas/instruction.cpp:686+`）汇编，**编译链路断裂** | ✅ PR #37：codegen 输出对齐 NCI v2.1，ncc→nas→nvm 链路打通 |
| S2 | VM 分发表 bug：POP(0x42) 挂在 `0x41` | `nvm/core.cpp:84` vs `nvm/instructions.hpp:51` | 所有含 POP 的字节码执行错误 | ✅ PR #31/#32：POP 归位 0x42（随 #32 的 LOADA/STOREA/PUSHI 接线顺带修正）；#31 同步落位 R4=SP/R5=BP 别名与栈底哨兵 |
| S3 | 缺失 handler：PUSHI/JN/JP/LOADA/STOREA/ANDI/ORI/XORI/SHLI/SHRI | `nvm/core.cpp:57-95` | 相关指令运行即 "Unknown op" | ✅ PR #32：10 条 handler 实现并接线（含编码测试） |
| S4 | `tests` 目标**编译失败**：`test_instructions.cpp` 使用已删除的 `NInstructions*` API；xmake 引用不存在的 `nvm/instructions.cpp` | `tests/test_instructions.cpp:9`、`xmake.lua:39` | 测试体系不可用（`xmake build tests` 报错） | ✅ 历史修复：PR #25 修复 tests 构建、重写 test_instructions、v1 用例暂挂（#39 迁回） |
| S5 | ncc CLI 是空壳 | `ncc/main.c:9-22` | 无法从命令行编译任何文件 | ✅ PR #30：CLI 接入 libca opt（多文件、-o、--emit、-MMD/-MF、--help/--version） |
| S6 | 语义分析完全未做 | `doc/需求设计文档.md:37`（⏳ 待开发） | 无类型检查，错误在后端才暴露 | ✅ PR #38：语义分析模块（作用域栈/类型检查/file:line:col 诊断） |
| S7 | 无 IR 层：AST → 文本汇编直通 | `ncc/codegen.cpp` | 无法支撑多后端 | ✅ PR #45：IR 数据模型 + AST 降级器 + dump；PR #48：NAS 后端迁移为消费 ir::Module |
| S8 | 语言只有 int 级子集：无字符串字面量、指针、`&`、数组、struct、typedef | `ncc/lexer.hpp:25-27`（仅 INTEGER/CHAR_CONSTANT）、`ncc/parser.cpp`（无 `*`/`[`/`&` 解析） | 无法表达 C ABI，`printf(char*)` 不可能 | ✅ PR #41：字符串字面量/指针/一维数组；PR #44：struct/typedef |
| S9 | nas 无数据段：`dataSize` 硬编码 0，无 `db` 指令 | `nas/main.cpp:86`、`nas/instruction.cpp` | 字符串常量无处安放，VM 后端无法做 extern 调用 | ✅ PR #34：nas 产出 v2.1 完整目标文件（数据段/导入导出表） |
| S10 | 文档三份互不一致 | 需求设计文档说"27 条+trap 系统调用"；开发计划说"~50 条无 syscall"；Bytecode 规范 v2.1 为准则 | 参照系混乱 | ✅ PR #36：v2.1 术语/表布局对齐；本轮：PRD/开发计划/README/AGENTS 全面进度对账 |
| S11 | 本机无 LLVM | `clang/llc/llvm-config` 均未安装；有 MSVC 2022 + cmake 4.3.3 | LLVM 后端需先解决环境 | ✅ PR #55：官方预编译包安装；后端交付（`ir::Module` → .ll，`NANOC_LLVM_DIR`/PATH llc 探测，CI 经 Chocolatey 装 LLVM） |

**结论（2026-09-28 原判定，已兑现）：M0 先修链路（S1–S5、S9），再谈新后端与新特性——M0–M5、M7 已全部交付，M6 进行中。**

---

## 3 总体架构

### 3.1 编译管线

```
多个 .nc 源文件（文件即模块）
   │  import/export 解析（R2a）
   ▼
Lexer ─► Parser ─► AST
   ▼
语义分析（R1）：类型检查 / 作用域 / 符号表 / extern 声明(R3) / include C 头文件(R9)
   ▼
IR（R1，带类型、可 dump 文本）
   ▼
前端变换 pass（与后端无关，实现一次三后端通用）：
   • defer 展开（R10）      • match 降解为比较+跳转（R11）
   • coro 状态机变换（R12）  • 模块内符号解析
   ▼
后端分发（R13 差分测试保证一致性）
   ├─ B0 NAS 后端  ─► .nas ─► nas ─► .nci ─► nvm（VM 侧自带链接器，R7）
   ├─ B1 C 后端    ─► .c   ─► cl/clang ─► exe（系统链接器）
   └─ B2 LLVM 后端 ─► .ll/.o ─► lld/link ─► exe
```

### 3.2 后端能力矩阵

| 能力 | B0 NAS/VM | B1 C | B2 LLVM |
|---|---|---|---|
| 数据段/字符串常量 | ✅ `db`/`dw`/`dd` + 数据段（S9，#34） | 天然支持 | 天然支持 |
| extern 调用 | CALLX + 导入表 | `#include`/声明 | `declare` |
| 多文件链接 | **自带链接器**（NCI 头 import/export 表） | 系统链接器 | 系统链接器 |
| 独立运行 | 需 nvm | ✅ 原生 exe | ✅ 原生 exe |
| 交付顺序 | M0 修复保留 | M2 最先落地 | M3 主产物 |

### 3.3 实现基座（R14）

编译器实现（ncc/nas/nvm 的 C++ 代码）渐进式采用 **libca**：

- 新代码强制：CLI→`opt`、文件与 import 路径→`fs`、错误处理→`Result<T,E>`、符号表/AST/IR→`collection`、日志→`log`
- 存量代码不强制重写（`nvm/string_helper.hpp`、现有 spdlog 调用碰到才换）
- 构建接入：`add_repositories("luiox-repo ...")` + `add_requires("libca", {configs={modules="core,str,collection,fs,opt,log"}})`

---

## 4 需求总览

状态图例：✅ 已交付 · 🔶 部分交付 · ⏳ 未开始/进行中。

| 编号 | 需求 | 优先级 | 依赖 | 里程碑 | 状态（2026-10-03） |
|---|---|---|---|---|---|
| R0 | 存量修复与 CLI 骨架（链路打通） | P0 | — | M0 | ✅ #25/#30/#31/#32/#37/#39（黄金 e2e + v1 用例迁回） |
| R14 | 实现基座 libca（随 M0 起生效） | P0 | — | M0 | ✅ 基座接入 #27（锁 0.0.8）；新代码必用、旧代码碰到才换，持续遵循 |
| R1 | 语义分析 + 类型系统扩展 + IR | P0 | R0 | M1 | ✅ #38（语义）/ #41、#44（类型）/ #45（IR）/ #48（NAS 后端迁移） |
| R2a | 多文件整体编译（import/export） | P0 | R1 | M1 | ✅ #46 |
| R3 | extern 声明（C ABI） | P0 | R1 | M2 | ✅ #52（C 后端带签名原型、printf 可用；VM 侧 cdecl 发射 + msvcrt 真宿主 e2e；VM printf 包装器仍属 P2） |
| R4 | C 后端 `--emit=c` | P0 | R1 | M2 | ✅ #47（emit + C/VM 差分验收）+ #52（CLI 接线） |
| R5 | xmake `rule("nanoc")` | P0 | R2a, R4 | M2 | ✅ #51（rule + hello_project 样例 + 接入指南） |
| R6 | LLVM 后端 `--emit=obj\|exe` | P0 | R1 | M3 | ✅ #55（`ir::Module` → 文本 .ll；CLI `--emit=llvm\|obj\|exe` 接线；`NANOC_LLVM_DIR` 探测；差分矩阵 llvm 列接入） |
| R7 | 独立编译 + 链接（含 VM 链接器） | P1 | R2a, R6 | M4 | ✅ #40（VM 链接器 `nas -r`：段合并/重定位/符号解析）+ #54（`loadStandalone` 轻装载独立编译、跨模块链接 e2e、`-MMD` 依赖追踪） |
| R8 | xmake 完整 `toolchain("nanoc")` | P1 | R6, R7 | M4 | ✅ #56（`toolchain("nanoc")` + `ncc-separate` 独立编译驱动 + toolchain_project 样例 + `doc/xmake-toolchain.md`） |
| R10 | defer | P1 | R1 | M5 | ✅ #57（词法/语法/语义检查 + IR 层展开，三后端免费获得） |
| R11 | 模式匹配 match | P1 | R1 | M5 | ✅ #57（常量/区间/多值/守卫/通配五类模式，IR 降解为比较+跳转链） |
| R12 | 协程 coro/yield（状态机） | P1 | R10 | M6 | ⏳ 进行中（M6 开发中，R10 defer 展开基建已就绪） |
| R9 | include C 头文件（声明子集） | P1 | R1（可与 M5/M6 并行） | M7 | ✅ #58（预处理 + 头文件声明子集解析 + 语义原型合并，三后端可用） |
| R13 | 三后端差分测试框架 | P0 | R4, R6 | M4（起） | ✅ #50 框架 + #55 llvm 列：13 程序 × vm/c/llvm 三列（后端可用性动态探测、skip 策略、锚点断言）；defer/match 差分行复用 harness（#57） |

---

## 5 详细需求

### R0 存量修复与 CLI 骨架（M0）

**目标：链路重新贯通，一切新需求的前置。**

- **R0.1 修复测试体系**：`tests` 目标可编译；`test_instructions.cpp` 重写为适配 `nas/instruction.cpp` 现有 API；移除 `xmake.lua:39` 失效引用；全部历史可救测试转绿。
- **R0.2 三方指令集对齐**：ncc codegen 输出改为 NCI v2.1 助记符（`trap`→`leave/ret`+extern exit、`jic`→`jz`+`cmp`、`eq/gt/le…`→`cmp`+条件跳转、`[BP-n]` 间接寻址→汇编器可接受的语法、立即数加减→`addi/subi`）。以 `doc/Bytecode Format Specification v2.1.md` 为唯一参照（S10 由本文裁决）。
- **R0.3 VM 修复**：POP 归位 `0x42`、补 PUSHI/JN/JP/LOADA/STOREA/ANDI/ORI/XORI/SHLI/SHRI handler。
- **R0.4 ncc CLI**：`ncc <files...> [-o out] [--emit=asm|c|llvm|obj|exe] [-MMD -MF file.d] [--dump-ir] [--version]`（实际交付口径；`nas`/`nvm` CLI 对齐，扩展名统一 `.nci`，`nvm` 支持 `--Xss` 与 `--host-lib`）。
- **R0.5 端到端黄金测试**：`examples/hello.nc` 全链路（ncc→nas→nvm）跑通并断言退出码/输出；examples 5 例全部纳入。
- **R0.6 libca 接入**：xmake 拉取 libca 包并编译通过（R14 起点，见 3.3）。

**验收**：`xmake build && xmake run tests` 绿；`ncc examples/hello.nc --emit=asm` → `nas` → `nvm` 退出码正确。

### R1 语义分析 + 类型系统扩展 + IR（M1）

**目标：三后端与所有特性的地基。**

- **R1.1 语义分析**：符号表（作用域栈）、类型检查、函数签名/调用一致性、未声明使用/重复定义报错、`return` 覆盖检查；诊断格式 `file:line:col: error: ...`。
- **R1.2 类型系统扩展**（为 R3/R9 与指针运算铺路）：
  - 字符串字面量 `""`（新增 token，落数据段）
  - 指针：声明 `int* p`、取址 `&`、解引用 `*`、指针算术（加减）
  - 一维数组：`int a[10]`、下标、传参退化为指针（多维数组不做）
  - `struct` 定义、`.` 与 `->` 成员访问、结构体赋值/传参
  - `typedef`
  - `char` 作为独立类型参与运算（提升规则明确）
- **R1.3 IR**：带类型的中间表示（树/三地址混合，**非 SSA**），提供 `dump()` 文本；覆盖当前全部语言结构。
- **R1.4 NAS 后端迁移**：codegen 改为「IR → 文本汇编」，行为与 M0 黄金测试回归一致（先对齐再扩展）。

**验收**：全部示例经 IR 路径产物与 M0 基线一致；语义错误用例（10+ 负例）诊断正确。

### R2a 多文件整体编译（M1）

**目标：文件即模块，一期整体编译。**

- **语法**：
  ```c
  import math;              // 解析为同目录 math.nc
  import "util/helpers.nc"; // 显式路径
  export int add(int a, int b) { ... }   // 未 export 的顶层符号模块私有
  ```
- **语义**：单编译单元内合并所有模块符号表；`main` 全局唯一；重复定义/环形 import/找不到文件 → 编译错误（报错含 import 链）；重复 import 幂等。
- **一期不做**：独立编译中间产物（归 R7）、包名/目录命名空间、可见性分级（只有 export/私有两态）。

**验收**：4 文件示例工程（main + math + util + strings）整体编译运行正确；循环 import、符号冲突负例报错。

### R3 extern 声明（M2）

```c
extern int puts(char* s);
extern void exit(int code);
extern int printf(char* fmt, ...);   // varargs：C 后端天然支持
```

- 声明不生成定义；调用点按 C ABI 生成；C 后端可变参 `printf` 必须可用。
- VM 后端 varargs（cdecl）标 P2，随 R7 导入表实现。

**验收**：NanoC 程序调用 `puts`/`printf`/`exit` 在 C 后端正确运行。

### R4 C 后端（M2）

- `ncc --emit=c`：IR → 可读 C（保留结构化输出与注释，便于调试与人工核对）。
- 类型映射表（`int→int32_t`、`char→int8_t`、`T*→T*`、struct→struct、字符串→`static const char[]`）。
- **定位为差分测试 oracle**（R13 基准）。

**验收**：examples 全部经 C 后端编译运行，结果与 VM 后端一致。

### R5 xmake `rule("nanoc")`（M2）

- SDK 提供 `rule("nanoc")`（扩展名 `.nc`），用户工程：
  ```lua
  add_rules("nanoc")   -- 或 add_requires 的 SDK 自动注入
  target("app")
      set_kind("binary")
      add_files("src/**.nc")
  ```
- 一期形态：目标级一次调用 `ncc`（整体编译）→ `.c` → 复用当前 C 工具链编译链接（即 ncc 当"前端预处理器"用）。
- 附：可运行的样例工程 `examples/hello_project/`（含其 `xmake.lua`）+ README 接入文档。

**验收**：样例工程在干净机器上 `xmake` 一条命令出 exe。

### R6 LLVM 后端（M3）

- **选型（开放问题 O1，已裁决）**：官方预编译 Windows 包；工具链探测 = 环境变量 `NANOC_LLVM_DIR` > PATH 上的 `llc` > 本机参考 SDK 路径（口径见 `ncc/src/ncc/llvm_backend.hpp` 头注）。
- `ncc --emit=llvm`（文本 .ll）、`--emit=obj`（目标文件）、`--emit=exe`（链接首选 lld-link、回退 clang 驱动）。
- IR → LLVM IR 映射：SSA 由 LLVM 管（mem2reg），NanoC IR 保持非 SSA；opaque pointers（LLVM 15+）。
- **交付（PR #55）**：后端与 CLI 接线落地；无 LLVM 环境时 `--emit=obj|exe` 给出含安装指引的明确报错；CI（windows-latest）经 Chocolatey 安装 LLVM，llvm 差分列真跑。

**验收**：examples 经 LLVM 后端产出 exe，运行结果与 C/VM 后端一致；无 LLVM 环境时 `--emit=llvm` 给出明确报错提示。

### R7 独立编译 + 链接（M4）

- **二期编译模型**：每模块独立产出中间产物（C 后端 = .c；LLVM 后端 = .o；VM 后端 = .nci 对象）。
- **VM 链接器**：复用 NCI v2.1 头预留的 `importCount/exportCount` 字段（`doc/Bytecode Format Specification v2.1.md:20`），填充导入表（符号→地址重定位）与导出表；已交付 `nas -r` 链接命令（#40，段合并/重定位/符号解析，见规范 §2.2）。
- C/LLVM 后端链接交系统链接器，ncc 只负责 emit 各模块的外部声明。
- 增量：`.d` 依赖文件 + xmake 已有文件追踪，改一模块只重编该模块。

**验收**：两模块程序独立编译 + 链接后运行正确；改动一模块仅重编该模块。

### R8 xmake 完整 `toolchain("nanoc")`（M4）

- 注册正式 toolchain：用户 `set_toolchains("nanoc")`，ncc 作为一等编译器按文件产 obj、按目标链接。
- 前置：R6 + R7（每文件产 `.obj` 的能力）。
- 与 R5 rule 并存（rule 面向一期整体编译，toolchain 面向正式形态），文档写清选择指南。

**验收**：样例工程切换 `set_toolchains("nanoc")` 后全量/增量构建均正确。

### R9 include C 头文件（M7）

- **支持**：`#include "relative/path.h"`（引号形式、相对当前文件）；`#pragma once`/`#ifndef` include guard 识别（简单跳过）。
- **不写完整预处理器**：`#define` 仅支持对象宏且值为字面量/常量表达式的替换；函数宏、条件编译 `#if/#ifdef` 不支持（识别后报"不支持"而非误编译）。
- **声明子集**：`typedef`、`struct`/`enum`、函数原型、指针、数组、`const/volatile/unsigned` 等修饰符（语义忽略或按 C 解释）、基础类型与固定宽度类型。
- **产出**：纳入语义分析符号表，供 `extern` 调用与结构体成员访问使用。
- **验收用例**：解析自写 C 库头文件子集；可选项（后续）：`luiox/libca-em`（C99）作为语言侧真实调用目标，当前本机未拉取该仓库，列为后续。

**验收**：`#include "mylib.h"` 后可按头文件签名调用其中函数、访问 struct 成员；不支持的预处理结构给出明确报错。

### R10 defer（M5）

- **语义**：作用域（含函数体、块）退出时**逆序**执行已注册 defer；覆盖正常退出、`return`、`break`、`continue`。
- **限制**：defer 内再 `return` → 编译错误；defer 注册表达式在注册时求值。
- **实现**：IR 层展开（在所有退出点插入 defer 序列），三后端免费获得。
- **验收**：嵌套作用域逆序执行顺序断言；return/break/continue 各路径覆盖；负例（defer 内 return）报错。

### R11 模式匹配 match（M5）

- **语法**：
  ```c
  int r = match (x) {
      0        => 1,
      1..9     => 2,        // 区间
      10, 11   => 3,        // 多值
      n if n < 0 => 4,      // 守卫 + 绑定
      _        => 0,        // default（等价通配）
  };
  ```
- **支持模式**：整数/字符常量、区间、多值、守卫（`if`）、通配 `_`；不引入 ADT/枚举变体（决策 A1）。
- **降解**：IR 层展开为比较+跳转链（密集值可提示编译器生成跳转表，属优化不做要求）。
- **检查**：未覆盖且无 `_` → 警告；类型不匹配（对字符串 match 等）→ 错误。
- **验收**：四类模式 + 嵌套/表达式形式用例；缺 default 警告；三后端结果一致。

### R12 协程 coro/yield（M6）

- **模型**：无栈协程 = 状态机变换（决策 A2）。
  ```c
  coro int counter(int n) {
      for (int i = 0; i < n; i++) yield i;
      return -1;
  }
  // 变换为：句柄结构体 { int state; int i; int n; ... }
  // resume(h) → switch(h.state) { case 0: ... case 1: ... }
  ```
- **语义**：`yield expr` 挂起并产出值；`resume` 恢复，函数结束返回完成态；局部变量提升到句柄；句柄生命周期由调用方管理（无 GC，手动作用域释放）。
- **交互规则（硬约束）**：`yield` 不得出现在 pending defer 的作用域内 → 编译期错误（决策 A6）。
- **实现**：IR 层变换，三后端通用。
- **验收**：迭代器式协程、多协程并发推进、跨函数 yield、×defer 负例、三后端结果一致。

### R13 三后端差分测试框架（M4 起）

- 测试矩阵：{examples + 新增特性用例} × {B0, B1, B2} → 断言退出码与 stdout 一致。
- 后端可用性动态探测（无 LLVM 环境自动跳过 B2 并标记 skip 而非失败）。
- 已接入 GitHub Actions CI（`.github/workflows/ci.yml`：windows-latest，构建 + 全量测试，含 LLVM 工具链安装与 llvm 差分列）。

### R14 实现基座 libca（M0 起，贯穿）

见 3.3。补充约束：

- libca 版本**锁小版本**（pre-1.0 无兼容保证，见 README 免责），升级走单独 issue。
- 存量迁移不单开任务，遵循"新代码必用、旧代码碰到才换"。

---

## 6 非功能需求

| 类别 | 要求 |
|---|---|
| 一致性 | 同一程序在可用后端上的退出码与 stdout 完全一致（R13 强制） |
| 错误信息 | 所有诊断带 `file:line:col`；import 错误带 import 链 |
| 平台 | Windows/MSVC 一等公民；Linux/gcc 二等（CI 后续补） |
| 性能 | 编译速度无硬指标（单人项目规模）；VM 执行效率不因 IR 化回退；增量构建目标：改单模块不全量重编 |
| 可维护性 | 每个需求带测试；新代码遵循 `.clang-format`；新代码使用 libca（R14） |
| 文档 | 每个里程碑更新对应文档；PRD 第 2 章问题清零后勾销 |

---

## 7 里程碑

```
M0 存量修复与端到端（R0, R14）
 └─ M1 地基：语义分析+类型扩展+IR+多文件（R1, R2a）
     ├─ M2 首次落地：C后端+extern+xmake rule（R3, R4, R5）── 语言首次真实可用
     ├─ M3 LLVM 后端（R6）
     │    └─ M4 独立编译+链接+完整 toolchain+差分测试（R7, R8, R13）
     ├─ M5 语言特性：defer + match（R10, R11）
     │    └─ M6 协程状态机（R12）
     └─ M7 外部库：include C 头文件（R9）※与 M5/M6 并行
```

| 里程碑 | 完成标志 | 状态（2026-10-03 终轮） |
|---|---|---|
| M0 | `xmake build && xmake run tests` 全绿；hello.nc 端到端跑通；libca 包接入 | ✅ 已完成（#25/#30/#31/#32/#37/#39 + libca #27） |
| M1 | 语义错误负例全捕获；4 文件 import 工程整体编译；IR dump 可读 | ✅ 已完成（#38/#41/#44/#45/#46/#48） |
| M2 | **样例工程一条 `xmake` 命令产出 exe（C 后端）**；printf 可调用 | ✅ 已完成（#47/#51/#52：rule 一条命令出 exe；extern 真调 msvcrt） |
| M3 | `ncc --emit=exe` 产出原生 exe；三后端结果一致 | ✅ 已完成（#55：LLVM 后端 + CLI 接线；差分矩阵 llvm 列三后端一致） |
| M4 | 独立编译+增量；`set_toolchains("nanoc")` 可用；差分矩阵跑绿 | ✅ 已完成（#40 `nas -r` + #54 独立编译/链接 e2e + #56 toolchain + #50/#55 差分矩阵 vm/c/llvm） |
| M5 | defer/match 特性用例三后端一致 | ✅ 已完成（#57：语义/IR/VM/三后端差分 28 项） |
| M6 | 协程迭代器示例三后端一致 | ⏳ 进行中（开发分支进行中） |
| M7 | `#include "x.h"` 声明子集可用 | ✅ 已完成（#58：预处理 + 声明子集 + 语义原型合并） |

> 当前测试基线：**465 项全绿**（48 个测试套件，含差分矩阵 vm/c/llvm 三列）。

---

## 8 测试策略

1. **单元测试（GoogleTest）**：每组件一文件（lexer/parser/semantic/ir/each backend/each feature）；R0 先恢复存量。
2. **黄金文件**：examples 编译产物与运行结果快照（文本类产物做规范化后比对）。
3. **差分矩阵**：R13，程序 × 后端一致性断言。
4. **负例集**：每个语义/特性错误必须有对应用例（如 defer 内 return、循环 import、yield 跨 defer）。
5. **每 issue 自带验收**：issue 模板要求列出测试方式，未带测试不关闭。

---

## 9 风险与开放问题

| # | 项 | 类型 | 处理 |
|---|---|---|---|
| O1 | LLVM 获取方式：官方预编译包 vs xmake-repo `llvm` 包（体积大、网络、版本固定） | 已裁决 | ✅ 落地官方预编译包 + `NANOC_LLVM_DIR`/PATH llc 探测（#55）；未单独成文 llvm-setup.md，口径见 `llvm_backend.hpp` 头注与 `.github/workflows/ci.yml` |
| O2 | C 头文件子集边界易失控（预处理器是无底洞） | 风险 | R9 明确"不做"清单；以解析自写 C 库为验收上限 |
| O3 | 协程×defer 交互复杂 | 风险 | 已用硬约束规避（yield 不得跨 pending defer），M6 首个负例 |
| O4 | VM 链接器 + 数据段是被低估的存量缺口（S9 + NCI 导入导出表） | 风险 | 拆进 R7 独立 issue，不与其他里程碑混排 |
| O5 | libca pre-1.0 API 变动 | 风险 | 锁小版本（R14） |
| O6 | 单人开发，8 里程碑串并行混排 | 风险 | 依赖图为准，M5/M6/M7 可按兴趣切换 |
| O7 | 旧文档（开发计划 NCIv2.1、需求设计文档）与本 PRD 冲突 | 风险 | 附录 A 裁决记录为准，冲突处以本文为准 |

---

## 10 明确不做（Out of Scope）

- 宏系统（#define 函数宏、条件编译、字符串化、拼接）
- 编译器优化器（仅允许 LLVM 自带优化；NanoC IR 不做优化 pass）
- GC / 自动内存管理（协程句柄、结构体由调用方管理）
- C++ 特性（类、模板、命名空间、异常）
- 运行时动态加载（dlopen 式）、运行时反射
- 多线程语言级支持（libca thread 是编译器实现侧设施，非语言特性）
- 完整 C 标准库移植、多维数组、位域、联合体 union、goto、变长数组
- IDE/LSP 支持、调试器集成
- Linux CI（后续单独议题，不阻塞本 PRD）

---

## 附录 A 决策记录

| # | 决策 | 结论 | 备选（弃） |
|---|---|---|---|
| A1 | 模式匹配范围 | 常量/区间/多值/守卫/通配 + default；不引入 ADT | 完整 ADT + 穷尽性证明；结构解构 |
| A2 | 协程模型 | 无栈协程 = 状态机变换 | 有栈协程（保存完整调用栈） |
| A3 | 外部库形态 | 一期 extern 声明 → 二期 include C 头文件（声明子集） | 仅 extern；两者并行 |
| A4 | 旧 VM 后端 | 三后端并存，共享前端+IR，含存量修复 | 冻结；彻底退役 |
| A5 | 模块语法 | 文件即模块 + `import`/`export` 关键字 | 显式 `module name;`；声明/实现文件分离（.nch） |
| A6 | 编译模型 | 先整体编译（M1-M3）→ 后独立编译+链接（M4） | 一步到位独立编译；永久整体编译 |
| A7 | xmake 集成 | 先 `rule`（M2）→ 后完整 `toolchain`（M4） | 仅 rule；rule+包分发 |
| A8 | LLVM 集成 | 官方预编译包 + llvm-config 探测（O1 待终选） | 自编译 LLVM；JIT（ORC） |
| A9 | C 后端定位 | 最快落地路径 + 差分测试 oracle | 仅教学用途 |
| A10 | libca 参与 | 编译器实现依赖 libca，渐进式（新代码必用、旧代码碰到才换） | 一次性全量迁移；仅外围用；不参与 |
| A11 | 指令集参照 | `Bytecode Format Specification v2.1` 为唯一准则 | 需求设计文档（27 条旧集）；开发计划草案 |

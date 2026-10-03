# NanoC

NanoC 是一个类似 C 语言子集的编译器 + 虚拟机项目，用于学习编译器与虚拟机的完整实现。当前已演进为**一个共享前端与 IR、三个编译后端并存**的形态（多后端路线图见 [PRD](doc/PRD-多后端编译与语言特性.md)）。

## 工具链与三个后端

```
                 ┌─ B0 NAS 后端 ──> .nas ──(nas)──> .nci ──(nvm)──> VM 执行
.nc 源码 ──ncc──>├─ B1 C 后端   ──> .c ──(cl/clang/gcc)──> 原生 exe
（前端+IR）       └─ B2 LLVM 后端 ─> .ll/.obj ──(lld/link)──> 原生 exe（开发中）
```

| 组件 | 说明 |
|------|------|
| **ncc** | Nano C 编译器：词法/语法分析 → AST → 语义分析（作用域/类型检查）→ IR（`--dump-ir` 可查看）→ 后端发射；支持多文件整体编译（`import`/`export`）与 `extern` 声明 |
| **nas** | 汇编器 + 链接器：汇编为 NCI v2.1 二进制（32 字节头 + 代码段 + 数据段 + 导入/导出表）；`nas -r` 多目标链接 |
| **nvm** | 虚拟机：48 条极简指令，无系统调用；CALLX 调用宿主 C 函数（静态注册、`--host-lib` 动态链接、链接器内部解析） |
| **C 后端** | `ncc --emit=c` 产出可读 C（`ir::Module` → C），交给系统 C 工具链编译为原生 exe，兼作差分测试 oracle |
| **LLVM 后端** | ⏳ 开发中（`--emit=llvm/obj/exe` 预留） |

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
# --host-lib 按符号名解析动态导入（可多次指定）
xmake run nvm hello_ext.nci --host-lib C:/Windows/System32/msvcrt.dll
# -> hello from NanoC
```

### C 后端（原生 exe）

```bash
xmake run ncc examples/arithmetic.nc --emit=c -o arithmetic.c
clang arithmetic.c -o arithmetic.exe   # 或 cl/gcc；运行结果与 VM 后端一致
```

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

## 构建

需要 [Xmake](https://xmake.io) 与 C++17 编译器（gtest/spdlog/libca 依赖自动安装）：

```bash
xmake            # 构建全部目标
xmake run tests  # 运行 GoogleTest 测试套件
```

常用命令与代码规范见 [AGENTS.md](AGENTS.md)。

## 目录结构

```
ncc/src/ncc/     编译器（lexer/parser/ast/semantic/ir/codegen/c_backend/loader）
nas/src/nas/     汇编器与链接器（instruction：编码与汇编；linker：nas -r）
nvm/src/nvm/     虚拟机（core：加载器与执行；handlers/：分指令 handler）
rules/nanoc/     xmake rule("nanoc")：.nc 接入 xmake 构建
tests/           GoogleTest 测试（support/ 为差分测试共用 harness）
examples/        示例 .nc 程序与 hello_project 样例工程
doc/             设计文档与规范
```

## 差分测试

`tests/test_diff_matrix.cpp`（PRD R13）把差分断言收敛为可扩展矩阵：
**13 个程序（examples 5 例 + 8 个特性用例）× 可用后端（vm/c）**，断言同一程序在
各后端的可观测退出码一致，并以已知 R0 锚点防止"一致地错"。后端可用性动态探测：
无 C 编译器（探针 `NANOC_C_COMPILER` > PATH clang > gcc）时该列记 SKIP 而非失败；
LLVM 后端列待 R6 接入。

## 文档

| 文档 | 内容 |
|------|------|
| [doc/Bytecode Format Specification v2.1.md](doc/Bytecode%20Format%20Specification%20v2.1.md) | NCI v2.1 字节码格式权威规范（指令集、文件布局、链接语义、调用约定） |
| [doc/PRD-多后端编译与语言特性.md](doc/PRD-多后端编译与语言特性.md) | 产品路线图（R0-R14，多后端与语言特性；含里程碑进度对账） |
| [doc/开发计划 NCIv2.1.md](doc/开发计划%20NCIv2.1.md) | NCI v2.1 基座五阶段计划（已完成；后续演进以 PRD 为准） |
| [doc/xmake-rule.md](doc/xmake-rule.md) | rule("nanoc") 接入指南与已知边界 |
| [doc/Complier Design Description.md](doc/Complier%20Design%20Description.md) | 工具链流水线与文件格式总览 |
| [doc/需求设计文档.md](doc/需求设计文档.md) | 功能/非功能需求与验收标准 |

## 当前状态

- ✅ NCI v2.1 指令集（48 条）与 VM 执行层、栈帧管理、栈底哨兵
- ✅ nas 汇编器 v2.1 完整目标文件 + 链接器（`nas -r`：段合并/重定位/符号解析）
- ✅ VM 加载器（严格校验、导入/导出表、数据段载入、`--host-lib` 动态链接与签名包装器）
- ✅ ncc 前端：语义分析、类型系统（字符串/指针/数组/struct/typedef）、IR、多文件 import/export
- ✅ C 后端 `--emit=c`（原生 exe，差分 oracle）；extern 声明（C 后端真调 msvcrt，VM 侧 cdecl）
- ✅ xmake `rule("nanoc")` + hello_project 样例：一条命令 `.nc → C → exe`
- ✅ 测试 392 项全绿（含黄金 e2e、链接器、宿主库、差分矩阵）；GitHub Actions CI
- ⏳ LLVM 后端（M3/R6）开发中；独立编译与 `toolchain("nanoc")`（M4/R7/R8）进行中

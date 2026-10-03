# NanoC

NanoC 是一个类似 C 语言子集的编译器 + 虚拟机项目，用于学习编译器与虚拟机的完整实现。

## 工具链

```
.nc 源码 ──(ncc)──> .nas 汇编 ──(nas)──> .nci 字节码 ──(nvm)──> 执行
```

| 组件 | 说明 |
|------|------|
| **ncc** | Nano C 编译器：词法/语法分析 → AST → 生成 .nas 汇编文本 |
| **nas** | 汇编器：两遍扫描汇编为 NCI v2.1 二进制（32 字节头 + 代码段 + 数据段 + 导入/导出表） |
| **nvm** | 虚拟机：48 条极简指令，无系统调用；CALLX 调用宿主 C 函数（静态注册或 GetProcAddress/dlsym 动态链接） |

## 快速上手

```bash
# 1. 编译 .nc 源码为汇编
xmake run ncc examples/arithmetic.nc -o arithmetic.nas

# 2. 汇编为字节码
xmake run nas arithmetic.nas arithmetic.nci

# 3. 虚拟机执行
xmake run nvm arithmetic.nci
```

带外部函数的程序（汇编侧声明，宿主侧提供实现）：

```asm
extern myadd 0x7F000001    ; 宿主地址静态绑定
export main

main:
    lmm R0, 7
    lmm R1, 5
    callx myadd            ; 调用宿主 C 函数，返回值 → R0
    ret
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
ncc/src/ncc/     编译器（lexer/parser/ast/codegen）
nas/src/nas/     汇编器（instruction：指令编码与整体汇编）
nvm/src/nvm/     虚拟机（core：加载器与执行；handlers/：分指令 handler）
tests/           GoogleTest 测试
examples/        示例 .nc 程序
doc/             设计文档与规范
```

## 文档

| 文档 | 内容 |
|------|------|
| [doc/Bytecode Format Specification v2.1.md](doc/Bytecode%20Format%20Specification%20v2.1.md) | NCI v2.1 字节码格式权威规范（指令集、文件布局、符号表、调用约定） |
| [doc/开发计划 NCIv2.1.md](doc/开发计划%20NCIv2.1.md) | 五阶段开发计划与当前进度 |
| [doc/PRD-多后端编译与语言特性.md](doc/PRD-多后端编译与语言特性.md) | 产品路线图（R0-R14，多后端与语言特性） |
| [doc/Complier Design Description.md](doc/Complier%20Design%20Description.md) | 工具链流水线与文件格式总览 |
| [doc/需求设计文档.md](doc/需求设计文档.md) | 功能/非功能需求与验收标准 |

## 当前状态

- ✅ NCI v2.1 指令集（48 条）与 VM 执行层、栈帧管理、栈底哨兵
- ✅ nas 汇编器 v2.1 完整目标文件（extern/export/调用约定/数据段）
- ✅ VM 加载器（严格校验、导入/导出表、数据段载入、宿主函数分发与动态链接）
- ✅ 集成 e2e：汇编 → 加载 → 宿主调用
- ⏳ v1 旧用例迁移、C 标准库互操作、ncc → nas → nvm 全工具链 e2e

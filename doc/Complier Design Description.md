# NanoC 工具链与文件格式

## 工具链流水线

```
.nc 源码 ──(ncc)──> .nas 汇编 ──(nas)──> .nci 字节码 ──(nvm)──> 执行
```

## 文件格式

### .nc（Nano C 源文件）

纯文本的 Nano C 源代码（C 语言子集），由 ncc 编译。

### .nas（NanoC 汇编文件）

文本格式的汇编代码，面向 NCI v2.1 指令集（48 条指令）。支持标号、
`extern`/`export`/`.calling_convention` 伪指令与 `db`/`dw`/`dd` 数据定义，
由 nas 汇编为 .nci 文件。

### .nci（NCI v2.1 目标文件）

二进制字节码，由 nvm 直接加载执行。布局：
`header(32B) | code | data | import table | export table`。
含导入表（CALLX 的宿主符号）与导出表（对外的代码符号）。
字节级定义见《Bytecode Format Specification v2.1》§2/§2.1。

## 组件

### ncc（Nano C Compiler）

把 .nc 编译为 .nas 汇编文本。支持多文件按序编译、`-o` 自定义输出、
`-MMD`/`-MF` 依赖文件生成；多后端（c/llvm/obj/exe）在 CLI 层预留。

### nas（NanoC Assembler）

把 .nas 两遍扫描汇编为 .nci：第一遍收集标号/符号表并线性编码，
第二遍回填标号地址与 CALLX 导入地址，产出完整 v2.1 目标文件。

### nvm（NanoC Virtual Machine）

极简虚拟机：无系统调用、无内置函数，仅 CALL/CALLX 两条调用指令。
加载 .nci 后从 entryPoint 执行；CALLX 调用宿主 C 函数（按地址静态注册，
或按符号名经 GetProcAddress/dlsym 动态解析），所有 I/O 能力来自宿主。

## 历史术语说明

早期文档中的 `nca`（字节码）/`nco`（目标文件）/TRAP 系统调用等术语属于
v1 时代设计，已废弃；v1 的 27 条指令集与 TRAP 已被 NCI v2.1 的 48 条指令
与宿主函数机制取代。

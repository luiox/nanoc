# NanoC 字节码格式规范 v2.1 (NCI)

## 1. 概述

NCI（NanoC Intermediate）是 NanoC 虚拟机的二进制字节码格式。它是一种紧凑的、可直接执行的二进制格式，支持外部符号引用，**所有功能通过调用 C 函数实现，虚拟机本身不提供任何内置函数**。

### 1.1 设计哲学

**极简主义虚拟机**：
- ❌ 无系统调用
- ❌ 无内置函数
- ❌ 无特殊指令
- ✅ 仅 2 条调用指令（CALL/CALLX）
- ✅ 所有功能通过 C 标准库实现

---

## 2. 文件格式（32 字节头）

```
偏移  大小  字段
0     6     magic = "NanoC\0"
6     2     reserved = 0
8     4     headerSize = 32
12    4     codeSize
16    4     dataSize
20    4     importCount
24    4     exportCount
28    4     entryPoint
```

**魔数**（8 字节对齐）：
```
4E 61 6E 6F 43 00 00 00
 N  a  n  o  C  \0  [保留]
```

### 2.1 段布局与符号表（钉死布局）

文件按以下顺序线性排布，所有多字节字段为小端：

```
header(32B) | code(codeSize) | data(dataSize) | import table | export table
```

**数据段统一编址**：代码地址空间为 `[0, codeSize)`；数据标号地址 = `codeSize + 段内偏移`。VM 加载时将数据段放置在统一内存的 `codeSize` 偏移处（参考实现为栈缓冲低端），保证 `LEA` 取址结果与 `LOAD`/`STORE`/`LOADA`/`STOREA` 的绝对寻址落在同一地址空间。

**导入表 entry**（重复 importCount 次）：

| 偏移 | 大小 | 字段 |
|------|------|------|
| 0 | 4 | nameLen：符号名字节数（不含 NUL） |
| 4 | nameLen | name：UTF-8 符号名 |
| 4+nameLen | 1 | NUL 终止符 |
| 动态 | 动态 | pad：补零至 4 字节对齐（以 entry 起始为基准） |
| 动态 | 4 | addr：宿主地址。静态绑定 = 显式指定地址；动态导入 = 汇编器分配的伪宿主地址（见"动态链接约定"）；**0 = 旧格式动态导入（留给加载期分配）** |
| 动态 | 4 | flags：bit0-1 = 调用约定（0=fastcall，1=cdecl），bit2 = 动态导入标记（1 = 加载期按符号名经宿主库解析），其余位必须为 0 |

**导出表 entry**（重复 exportCount 次）：与导入表同构——addr = 符号在代码段内的地址，flags 恒 0。

**entryPoint 规则**：存在 `main` 标号则用之；否则取第一个导出符号的地址；否则 0。

**调用约定**：汇编侧 `.calling_convention fastcall|cdecl` 顺序作用于其后声明的 `extern`（文件级顺序生效），写入对应导入 entry 的 flags。fastcall 前 4 个整型参数走 R0-R3；cdecl 参数压栈、由调用者清栈（`addi R4, N`）。约定仅是符号元数据，指令编码不受影响。

**语言侧发射约定（ncc，PR #52）**：语言级 `extern` 声明（PRD R3）由 ncc 发射为无地址动态导入 `extern name`（nas 分配伪宿主地址并置 flags bit2，加载期经 `--host-lib` 按名解析）。带 `...`（varargs）的 extern 符号发射为 `.calling_convention cdecl` 环绕 `extern name`，随后立即发射 `.calling_convention fastcall` 恢复缺省——因 `.calling_convention` 文件级顺序生效，不恢复会误染其后声明的 extern。对应调用点序列：varargs extern 实参**自右向左压栈**（首参最后压、留在栈顶），`callx` 后 `addi R4, 4*N`（N = 实参个数）调用者清栈；非 varargs extern 仍走 fastcall（前 4 参弹入 R0-R3，第 5 参起压栈、调用后清 `addi R4, 4*(N-4)`）。

**动态链接约定**：导入 entry `flags bit2 = 1` 表示动态导入。nas 对无地址的 `extern name` 分配**确定性伪宿主地址**——从 `0x7E000000`（`DYNAMIC_HOST_BASE`）起、按声明序 +4——同时写入导入表 addr 字段与 `callx` 站点 imm（同值），站点↔符号一对一，消除旧格式 addr=0 的按值歧义；伪地址区与代码/数据地址空间及 VM 宿主地址分配区（`0x7F000000` 起，`HOST_ADDRESS_BASE`）隔离。显式地址 `extern name addr` 为静态宿主绑定（不置 bit2）。

加载期宿主库（`--host-lib <path>`，可多次）对动态导入按符号名 `GetProcAddress`/`dlsym` 解析：命中后将真 C 函数经**签名包装器**适配，登记在该导入的伪宿主地址上（`callx` 站点 imm 天然命中，无需改写）；未命中则明确报错（符号名 + 库名）。真 C 函数指针与 VM 宿主函数签名（`int32_t(*)(int32_t* regs, int8_t* mem, int32_t memSize)`）ABI 不同，不能直接 cast 调用——参考实现维护已知签名白名单（一期：`puts`/`putchar`/`abs`/`atoi`/`strlen`/`exit`/`GetTickCount`），库中存在但不在白名单的符号同样明确报错。字符串参数为 VM 统一内存地址，包装器内做边界保护（越界或非 NUL 终止视为无效参数）。

旧格式兼容：`addr = 0` 且 bit2 = 0 的导入仍按"加载期按名解析、从 `HOST_ADDRESS_BASE` 起分配宿主地址并回填导入表"处理（此形态下 `callx` 站点 imm 不自动命中，需手工内联分配后地址或依赖宿主侧按值约定）。

### 2.2 链接语义（nas -r）

多个 .nci 目标文件按命令行顺序链接为一个可执行文件（`nas -r a.nci b.nci -o out.nci`）：

- **段合并**：code/data 顺序拼接。统一编址下模块 i 的代码基址 = Σ前面 codeSize，数据基址 = Σ前面 (codeSize+dataSize)。
- **重定位**：按 §3.1 指令长度表线性解码 code 段，地址类指令 imm 做范围判断——`< 模块 codeSize` → 代码地址（+代码基址）；`∈ [codeSize, codeSize+dataSize)` → 数据地址（+数据基址）；`≥ codeSize+dataSize` → 宿主地址（不动）。CALLX 不做范围平移（见下）。数据段不做扫描重定位（无逐字重定位信息，`dd` 地址常量链接后失效；跨模块数据引用走"导出数据标号 + 导入解析"路径）。
- **符号解析**：导入 `addr = 0`（旧格式动态）或 `flags bit2 = 1`（伪地址动态导入）且名字命中任一模块导出 → 内部解析（**内部导出优先于加载期宿主解析**）：该模块内 imm 与导入 addr 相等的 CALLX/LEA/LOADA/STOREA/ST 站点改写为平移后目标地址，导入从输出表移除。未内部解析的动态导入，其站点 imm 统一改写为该符号在输出导入表中的首现伪地址（跨模块声明序差异归一）。显式静态宿主绑定（bit2 = 0 且 addr≠0）不改写。同一模块多个导入共享同一 addr 值且存在可内部解析者时站点无法按值消歧 → 报错（伪地址按声明序唯一，正常输入不触发）。未解析导入按名去重合并（flags 冲突报错），addr 取首现值；导出地址平移后合并（重名报错）。
- **entryPoint**：任一模块导出 `main` → 用之；否则第一个导出符号；否则 0。

**CALLX 双语义**：链接后 `0 < addr < codeSize` 的 CALLX 按内部 CALL 处理（压返回地址、跳转）；否则查宿主函数表（地址 0 保留给未解析动态导入的宿主路径，宿主地址不得落入 `[0, codeSize)`）。

---

## 3. 指令集（精简版）

### 3.1 完整操作码表

#### 内存访问（0x00-0x0F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x00 | LMM | R, IMM32 | 6 |
| 0x01 | ST | R, ADDR32 | 6 |
| 0x02 | LEA | R, ADDR32 | 6 |
| 0x03 | LOAD | R1, R2 | 3 |
| 0x04 | STORE | R1, R2 | 3 |
| 0x05 | LOADA | R, IMM32 | 6 |
| 0x06 | STOREA | R, IMM32 | 6 |

#### 算术运算（0x10-0x1F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x10 | ADD | R1, R2 | 3 |
| 0x11 | ADDI | R, IMM32 | 6 |
| 0x12 | SUB | R1, R2 | 3 |
| 0x13 | SUBI | R, IMM32 | 6 |
| 0x14 | MUL | R1, R2 | 3 |
| 0x15 | MULI | R, IMM32 | 6 |
| 0x16 | DIV | R1, R2 | 3 |
| 0x17 | DIVI | R, IMM32 | 6 |
| 0x18 | MOD | R1, R2 | 3 |
| 0x19 | MODI | R, IMM32 | 6 |
| 0x1A | NOT | R | 2 |
| 0x1B | NEG | R | 2 |

#### 逻辑运算（0x20-0x2F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x20 | AND | R1, R2 | 3 |
| 0x21 | ANDI | R, IMM32 | 6 |
| 0x22 | OR | R1, R2 | 3 |
| 0x23 | ORI | R, IMM32 | 6 |
| 0x24 | XOR | R1, R2 | 3 |
| 0x25 | XORI | R, IMM32 | 6 |
| 0x26 | SHL | R1, R2 | 3 |
| 0x27 | SHLI | R, IMM8 | 3 |
| 0x28 | SHR | R1, R2 | 3 |
| 0x29 | SHRI | R, IMM8 | 3 |

#### 比较（0x30-0x3F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x30 | CMP | R1, R2 | 3 |
| 0x31 | CMPI | R, IMM32 | 6 |
| 0x32 | TEST | R1, R2 | 3 |

#### 栈操作（0x40-0x4F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x40 | PUSH | R | 2 |
| 0x41 | PUSHI | IMM32 | 5 |
| 0x42 | POP | R | 2 |
| 0x43 | ENTER | IMM16 | 3 |
| 0x44 | LEAVE | - | 1 |

#### 控制流（0x50-0x5F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x50 | JMP | ADDR32 | 5 |
| 0x51 | JZ | ADDR32 | 5 |
| 0x52 | JNZ | ADDR32 | 5 |
| 0x53 | JN | ADDR32 | 5 |
| 0x54 | JP | ADDR32 | 5 |

#### 函数调用（0x60-0x6F）**核心指令**
| 码值 | 指令 | 格式 | 长度 | 描述 |
|------|------|------|------|------|
| 0x60 | CALL | ADDR32 | 5 | 内部调用 |
| 0x61 | CALLX | IMM32 | 5 | 外部调用；链接后内部地址（0<addr<codeSize）按 CALL 处理 |
| 0x62 | RET | - | 1 | 返回 |

#### 寄存器操作（0x70-0x7F）
| 码值 | 指令 | 格式 | 长度 |
|------|------|------|------|
| 0x70 | MOV | R1, R2 | 3 |
| 0x71 | CLR | R | 2 |
| 0x7F | NOP | - | 1 |

---

## 4. 寄存器约定

### 4.1 寄存器角色

| 寄存器 | 编码 | 用途 | 保护约定 |
|--------|------|------|----------|
| R0 (AX) | 0x00 | 返回值、参数 1 | 调用者 |
| R1 (BX) | 0x01 | 参数 2 | 调用者 |
| R2 (CX) | 0x02 | 参数 3 | 调用者 |
| R3 (DX) | 0x03 | 参数 4 | 调用者 |
| R4 (SP) | 0x04 | **栈指针** | 被调用者 |
| R5 (BP) | 0x05 | **基址指针** | 被调用者 |
| R6 | 0x06 | 通用 | 被调用者 |
| R7 | 0x07 | 通用 | 被调用者 |

### 4.2 调用约定

#### fastcall（默认）
```asm
; func(1, 2, 3, 4)
lmm R0, 1
lmm R1, 2
lmm R2, 3
lmm R3, 4
call func
; 返回值 → R0
```

#### cdecl（可变参数）
```asm
; printf("fmt", 1, 2)
lmm R0, 2
push R0
lmm R0, 1
push R0
lea R0, .fmt
push R0
callx printf
addi R4, 12      ; 清理栈
```

---

## 5. 示例

### 5.1 Hello World

```asm
extern printf
extern exit
export main

main:
    enter 0
    
    ; printf("Hello\n")
    lea R0, .msg
    push R0
    callx printf
    addi R4, 4
    
    ; exit(0)
    lmm R0, 0
    push R0
    callx exit
    
    leave
    ret

.msg:
    db "Hello\n", 0
```

### 5.2 递归阶乘

```asm
export factorial

factorial:
    enter 0
    ; R0 = n
    push R0
    lmm R1, 1
    cmp R0, R1
    pop R0
    jp .recurse      ; n > 1 → 递归
    lmm R0, 1        ; n <= 1 → 返回 1
    leave
    ret

.recurse:
    push R0
    lmm R1, 1
    sub R0, R1
    call factorial
    pop R1
    mul R0, R1
    leave
    ret
```

---

## 6. 实现清单

- [x] 指令定义：LOAD, STORE, ENTER, LEAVE, CALLX, MOV, CLR（v2.1 复活期主干直改；
      分发表落位与 POP 0x42 归位见 PR #31/#32）
- [x] 指令分离：ADD/ADDI, SUB/SUBI, MUL/MULI 等（v2.1 复活期主干直改；PUSHI 与
      ANDI/ORI/XORI/SHLI/SHRI/JN/JP 补齐见 PR #32）
- [x] **彻底移除：TRAP, SYSCALL, 所有系统调用**（v2.1 指令集无系统调用）
- [x] VM 执行：CALL/CALLX/RET, ENTER/LEAVE（复活期主干直改；R4=SP/R5=BP 寄存器
      别名与栈底哨兵返回地址见 PR #31）
- [x] VM 加载：v2.1 文件头/导入导出表/数据段/宿主分发/动态链接（PR #33）
- [x] NAS：`.calling_convention`, `extern`, `export`，数据段与完整 v2.1 目标文件（PR #34）
- [x] 集成验收：汇编 → 加载 → 宿主调用 e2e（PR #35）
- [x] VM 链接器：`nas -r` 段合并/重定位/符号解析 + CALLX 双语义（PR #40，PRD R7）
- [x] 宿主库直调通路：nas 动态导入伪宿主地址 + flags bit2、`nvm --host-lib` 按名解析 +
      签名包装器白名单（PR #42；R3 VM 侧前置）
- [x] 语言级 extern 声明：ncc 发射动态导入 + varargs cdecl 调用序列，msvcrt 真宿主
      e2e（abs/atoi/strlen/puts/exit）（PR #52）
- [ ] 测试：C 标准库互操作扩展（printf/malloc 等更多签名包装器；printf 属 VM varargs
      P2，随 R7 导入表扩展）

---

**设计原则**：虚拟机只提供基本指令，所有功能通过 C 函数实现。

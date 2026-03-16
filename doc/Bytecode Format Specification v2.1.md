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
| 0x61 | CALLX | IMM32 | 5 | 外部调用 |
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
    
    cmpl R0, 1
    jg .recurse
    lmm R0, 1
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

- [ ] 指令定义：LOAD, STORE, ENTER, LEAVE, CALLX, MOV, CLR
- [ ] 指令分离：ADD/ADDI, SUB/SUBI, MUL/MULI 等
- [ ] **彻底移除：TRAP, SYSCALL, 所有系统调用**
- [ ] VM 执行：CALL/CALLX/RET, ENTER/LEAVE
- [ ] NAS：`.calling_convention`, `extern`, `export`
- [ ] 测试：C 互操作（printf, malloc, exit）

---

**设计原则**：虚拟机只提供基本指令，所有功能通过 C 函数实现。

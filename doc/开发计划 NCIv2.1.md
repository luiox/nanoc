# NanoC NCI v2.1 开发计划

## 项目目标

实现基于 NCI v2.1 字节码格式的完整虚拟机系统，支持：
- 极简指令集（仅 CALL/CALLX 调用，无系统调用）
- 双调用约定（fastcall/cdecl）
- 静态/动态链接
- C 语言完全互操作

---

## 开发阶段

### Phase 1: 指令层实现 ✅

**任务**：
- [x] 更新 `nvm/instructions.hpp` - 新指令集定义（opcode 权威定义最终落在 `nas/instruction.hpp`，nvm 侧重复定义已去重，见 PR #35）
- [x] 实现指令解析和二进制生成（`nas/src/nas/instruction.cpp`，`Assembler::parseLine`/`assemble`）
- [x] 删除 TRAP 相关代码（v2.1 指令集无系统调用）

**指令清单**（共 48 条）：
1. 内存访问：LMM, ST, LEA, LOAD, STORE, LOADA, STOREA (7 条)
2. 算术运算：ADD, ADDI, SUB, SUBI, MUL, MULI, DIV, DIVI, MOD, MODI, NOT, NEG (12 条)
3. 逻辑运算：AND, ANDI, OR, ORI, XOR, XORI, SHL, SHLI, SHR, SHRI (10 条)
4. 比较：CMP, CMPI, TEST (3 条)
5. 栈操作：PUSH, PUSHI, POP, ENTER, LEAVE (5 条)
6. 控制流：JMP, JZ, JNZ, JN, JP (5 条)
7. 函数调用：CALL, CALLX, RET (3 条)
8. 寄存器操作：MOV, CLR, NOP (3 条)

**验收标准**：
- 每条指令实现编码生成（`emit()`，`Assembler::assemble` 整体两遍扫描）
- 每条指令实现汇编文本解析（`Assembler::parseLine`）
- 通过单元测试（PR #32 起 68 项基线，PR #34 增至 78 项）

---

### Phase 2: VM 执行层 ✅

**任务**：
- [x] 更新 `nvm/core.hpp` - flags 寄存器、新执行函数（PR #31：R4=SP/R5=BP 寄存器引用别名；PR #32：10 条新指令 handler）
- [x] 实现 `nvm/core.cpp` - 所有指令执行逻辑（handlers/ 分文件）
- [x] 实现 ENTER/LEAVE 栈帧管理
- [x] 实现 CALL/CALLX 调用逻辑
- [x] 实现条件跳转（JZ/JNZ/JN/JP）
- [x] 栈底哨兵返回地址：main 顶层 `leave`/`ret` 落到代码段末尾自然终止（PR #31）

**关键实现**：
```cpp
// flags 寄存器
int32_t m_flags;  // bit0=Z, bit1=N, bit2=P

// 寄存器角色（规范 v2.1 §4.1）：SP/BP 是通用寄存器的引用别名
int32_t & m_sp;   // == m_registers[4]
int32_t & m_bp;   // == m_registers[5]
```

**验收标准**：
- 能执行包含函数调用的字节码
- 栈帧正确建立和清理
- 递归调用正常工作（test_vm.cpp 组合用例）

---

### Phase 3: NAS 汇编器升级 ✅

**任务**：
- [x] 支持 `.calling_convention` 指令（顺序作用于其后 extern，写入导入表 flags）
- [x] 支持 `extern 符号 [地址]` 语法（缺省地址 0 = 留给动态链接）
- [x] 支持 `export 符号` 语法
- [x] 生成 NCI v2.1 格式（32 字节头 + 代码段 + 数据段 + 导入表 + 导出表）
- [x] 支持静态链接（CALLX 地址按导入表回填；标号地址统一编址回填）
- [x] db/dw/dd 数据段定义（字符串转义、dd 标号地址常量）

**文件布局与表 entry 字节图**：见《Bytecode Format Specification v2.1》§2.1（钉死布局，两侧实现逐字节一致）。

**验收标准**：
- 能汇编包含 extern/export 的文件（PR #34，字节级测试 10 项）
- 生成正确的 NCI v2.1 格式（Hello World 产物逐字节核对）
- 导入表地址正确填充

---

### Phase 4: VM 加载器升级 ✅

**任务**：
- [x] 解析 NCI v2.1 文件头（32 字节，含 headerSize 校验与长度合法性）
- [x] 解析导入表
- [x] 解析导出表
- [x] 支持动态链接（Windows GetProcAddress 实测；POSIX dlopen/dlsym 分支已实现未强测）

**加载流程**：
```
1. 读取 32 字节文件头，校验魔数 "NanoC\0\0\0" 与 headerSize==32
2. 校验各段/表长度合法（不超文件、entryPoint ∈ [0, codeSize]）
3. 解析导入表与导出表
4. 代码段载入 m_code；数据段载入统一内存 codeSize 偏移处
5. PC = entryPoint，开始执行
```

宿主函数机制（PR #33）：
- `registerHostFunction(addr, fn)` 静态绑定 / 按名注册供动态解析
- `loadHostLibrary(path)` 批量解析 addr=0 的导入符号，地址从 `0x7F000000` 起分配
- `CALLX` 命中宿主表 → 直接调用 C 函数（不压返回地址，返回值写 R0）；未命中 → 报错停止

**验收标准**：
- 能加载 v2.1 格式文件（test_loader.cpp，15 项）
- 外部函数地址正确解析（GetTickCount 真机动态链接用例）
- 程序正常执行

---

### Phase 5: 测试验证 ✅

**测试用例**：
- [x] v1 旧用例迁移：6 个 DISABLED_ 前缀的 v1 指令执行用例改写为 v2 指令集后恢复（PR #39）
- [x] C 标准库互操作：`--host-lib` 按名解析 + 签名包装器白名单（PR #42），msvcrt.dll 真宿主 e2e
      （abs/atoi/strlen/puts/exit，PR #52）；printf/malloc 等更多签名包装器待后续
      （PRD R3 将 VM varargs 列为 P2，随 R7 导入表扩展）
- [x] ncc → nas → nvm 全工具链 e2e：examples 全量黄金用例（PR #39）
- [ ] 性能测试：对比文本格式（未排期，后续演进以 PRD 为准）
- [x] 单元测试：每条指令（PR #32/#34）
- [x] 函数调用测试：参数传递、返回值（PR #33）
- [x] 递归测试：组合程序走通 call/leave/ret + 哨兵终止（PR #32）
- [x] 集成 e2e：汇编 → 加载 → 宿主调用（PR #35）

---

## 提交策略

**小步提交**：
- 每实现 1-2 条指令 → 提交一次
- 每完成一个模块 → 提交一次
- 每通过一轮测试 → 提交一次

**提交信息规范**：
```
feat: 实现 ADD/ADDI 指令
- 添加 NInstructionsADD 类
- 实现 generateInstructionCode()
- 实现 parserInstructionText()
- 添加单元测试

feat: 实现 ENTER/LEAVE 栈帧管理
- 添加 executeENTER()
- 添加 executeLEAVE()
- 通过递归测试
```

---

## 时间估算

| 阶段 | 预计时间 | 实际 |
|------|---------|------|
| Phase 1 | 2 天 | ✅ 已完成 |
| Phase 2 | 2 天 | ✅ 已完成 |
| Phase 3 | 2 天 | ✅ 已完成 |
| Phase 4 | 1 天 | ✅ 已完成 |
| Phase 5 | 1 天 | ✅ 已完成（PR #39 收尾） |

---

### Phase 6+: PRD 承接（本文档不再单列计划）

Phase 1–5 交付的「指令集 → VM → 汇编器 → 加载器 → 测试」基座已被
`doc/PRD-多后端编译与语言特性.md` 承接为 M0。此后的功能演进（多后端、
语义/类型/IR、多文件、extern、链接器、构建集成、语言特性等）不再在本文
单列阶段，其承接关系如下：

| 本计划成果 | PRD 承接 |
|---|---|
| Phase 1–4（指令集/VM/汇编器/加载器，PR #31–#34） | PRD M0（R0 存量修复）与 R3/R7 的 VM 侧基座 |
| Phase 5（黄金 e2e/集成 e2e，PR #35/#39） | PRD R13 差分测试与每需求测试策略 |

> **后续演进以 PRD 为准**：里程碑进度见 PRD 第 4/7 章（M0–M5、M7 已交付，
> M6 协程进行中）。本文仅作 NCI v2.1 基座的历史记录与指令集/格式
> 的实现注记，避免两份文档继续漂移。

---

## 风险点

1. **CALLX 外部调用**：✅ 已解决——宿主地址空间与代码/数据隔离（`HOST_ADDRESS_BASE` 起），未解析地址报错停止
2. **栈对齐**：调用 C 函数时需保持 4 字节对齐（宿主函数签名经 `(regs, mem, memSize)` 隔离，未见问题）
3. **寄存器保护**：callee-saved（R4-R7）约定已写入规范 §4.1，编译器后端接入时验证
4. **动态链接**：Windows GetProcAddress 已实测；Linux dlsym 分支待强测

---

**当前状态**：Phase 1–5 已完成（PR #25–#35；Phase 5 由 PR #39 收尾），后续演进以 PRD 为准
**下一步**：见 `doc/PRD-多后端编译与语言特性.md` 第 7 章——M0–M5、M7 已交付（多后端/独立编译/toolchain/defer/match/include），M6（R12 协程）进行中

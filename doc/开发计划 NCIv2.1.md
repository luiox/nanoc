# NanoC NCI v2.1 开发计划

## 项目目标

实现基于 NCI v2.1 字节码格式的完整虚拟机系统，支持：
- 极简指令集（仅 CALL/CALLX 调用，无系统调用）
- 双调用约定（fastcall/cdecl）
- 静态/动态链接
- C 语言完全互操作

---

## 开发阶段

### Phase 1: 指令层实现

**任务**：
- [ ] 更新 `nvm/instructions.hpp` - 新指令集定义
- [ ] 实现 `nvm/instructions.cpp` - 解析和二进制生成
- [ ] 删除 TRAP 相关代码

**指令清单**（共约 50 条）：
1. 内存访问：LMM, ST, LEA, LOAD, STORE, LOADA, STOREA (7 条)
2. 算术运算：ADD, ADDI, SUB, SUBI, MUL, MULI, DIV, DIVI, MOD, MODI, NOT, NEG (12 条)
3. 逻辑运算：AND, ANDI, OR, ORI, XOR, XORI, SHL, SHLI, SHR, SHRI (10 条)
4. 比较：CMP, CMPI, TEST (3 条)
5. 栈操作：PUSH, PUSHI, POP, ENTER, LEAVE (5 条)
6. 控制流：JMP, JZ, JNZ, JN, JP (5 条)
7. 函数调用：CALL, CALLX, RET (3 条)
8. 寄存器操作：MOV, CLR, NOP (3 条)

**验收标准**：
- 每条指令实现 `generateInstructionCode()` 
- 每条指令实现 `parserInstructionText()`
- 通过单元测试

---

### Phase 2: VM 执行层

**任务**：
- [ ] 更新 `nvm/core.hpp` - flags 寄存器、新执行函数
- [ ] 实现 `nvm/core.cpp` - 所有指令执行逻辑
- [ ] 实现 ENTER/LEAVE 栈帧管理
- [ ] 实现 CALL/CALLX 调用逻辑
- [ ] 实现条件跳转（JZ/JNZ/JN/JP）

**关键实现**：
```cpp
// flags 寄存器
int32_t m_flags;  // bit0=Z, bit1=N, bit2=P

// ENTER 执行
void executeENTER() {
    push(m_bp);       // 保存旧 BP
    m_bp = m_sp;      // 更新 BP
    int16_t size = fetch16();
    m_sp -= size;     // 分配局部变量
}

// CALL 执行
void executeCALL() {
    int32_t addr = fetch32();
    push(m_pc + 5);   // 保存返回地址
    m_pc = addr;
}

// CALLX 执行
void executeCALLX() {
    int32_t addr = fetch32();
    push(m_pc + 5);
    m_pc = addr;      // 跳转到外部函数
}
```

**验收标准**：
- 能执行包含函数调用的字节码
- 栈帧正确建立和清理
- 递归调用正常工作

---

### Phase 3: NAS 汇编器升级

**任务**：
- [ ] 支持 `.calling_convention` 指令
- [ ] 支持 `extern 符号 地址` 语法
- [ ] 支持 `export 符号` 语法
- [ ] 生成 NCI v2.1 格式（32 字节头 + 导入表 + 导出表）
- [ ] 支持静态链接（地址填充）

**语法示例**：
```asm
extern printf 0x08049000
extern exit 0x0804A000
export main

main:
    enter 0
    lea R0, .msg
    push R0
    callx printf
    addi R4, 4
    lmm R0, 0
    callx exit
    leave
    ret

.msg:
    db "Hello\n", 0
```

**验收标准**：
- 能汇编包含 extern/export 的文件
- 生成正确的 NCI v2.1 格式
- 导入表地址正确填充

---

### Phase 4: VM 加载器升级

**任务**：
- [ ] 解析 NCI v2.1 文件头（32 字节）
- [ ] 解析导入表
- [ ] 解析导出表
- [ ] 支持动态链接（dlsym/GetProcAddress）

**加载流程**：
```
1. 读取 32 字节文件头
2. 验证魔数 "NanoC\0"
3. 读取导入表，解析符号地址
4. 读取导出表，记录导出函数
5. 加载代码段到内存
6. 设置 PC=entryPoint，开始执行
```

**验收标准**：
- 能加载 v2.1 格式文件
- 外部函数地址正确解析
- 程序正常执行

---

### Phase 5: 测试验证

**测试用例**：
- [ ] 单元测试：每条指令
- [ ] 函数调用测试：参数传递、返回值
- [ ] 递归测试：阶乘、斐波那契
- [ ] C 互操作测试：printf/malloc/free
- [ ] 性能测试：对比文本格式

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

| 阶段 | 预计时间 | 提交次数 |
|------|---------|---------|
| Phase 1 | 2 天 | 10-15 次 |
| Phase 2 | 2 天 | 8-10 次 |
| Phase 3 | 2 天 | 6-8 次 |
| Phase 4 | 1 天 | 3-4 次 |
| Phase 5 | 1 天 | 4-5 次 |
| **总计** | **8 天** | **30-40 次** |

---

## 风险点

1. **CALLX 外部调用**：需要确保 VM 能正确跳转到 C 函数
2. **栈对齐**：调用 C 函数时需保持 4 字节对齐
3. **寄存器保护**：确保 callee-saved 寄存器正确恢复
4. **动态链接**：Windows/Linux API 差异

---

**当前状态**：准备开始 Phase 1
**下一步**：实现指令定义（instructions.hpp）

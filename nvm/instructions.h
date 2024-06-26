#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

enum Instructions {
    /* 保存和加载的指令 */
    Instructions_IMM,  /* Load Immediate */
    Instructions_LEA,  /* Load Effective Address */
    Instructions_LC,   /* Load Int */
    Instructions_LI,   /* Load Char */
    Instructions_SI,   /* Save Int */
    Instructions_SC,   /* Save Char */
    Instructions_PUSH, /* Push 将寄存器数据压到栈顶 */
    Instructions_POP,  /* Pop 弹出栈顶 */
    /* 运算 */
    Instructions_ADD,
    Instructions_SUB,
    Instructions_MUL,
    Instructions_DIV,
    Instructions_MOD,
    Instructions_OR,
    Instructions_XOR, /* Xor 按位异或 */
    Instructions_AND, /* And 逻辑与 */
    Instructions_SHL, /* Shift Logical Left 逻辑左移 */
    Instructions_SHR, /* Shift Logical Right 逻辑右移 */
    Instructions_EQ,  /* Equal 相等 */
    Instructions_NE,  /* Not Equal 不相等 */
    Instructions_LT,  /* Less Than 小于*/
    Instructions_LE,  /* Less Equal 小于等于*/
    Instructions_GT,  /* Greater Than 大于*/
    Instructions_GE,  /* Greater Equal 大于等于*/
    /* 分支跳转 */
    Instructions_JMP,
    Instructions_JE,
    Instructions_JNE,
    Instructions_CALL,
    Instructions_NVAR,
    Instructions_DARG,
    Instructions_RET,
    /* Native_Call */
    Instructions_OPEN,
    Instructions_CLOS,
    Instructions_READ,
    Instructions_WRIT,
    Instructions_PRTF,
    Instructions_MALC,
    Instructions_FREE,
    Instructions_MSET,
    Instructions_MCMP,
    Instructions_EXIT
};

// 根据指令名分发运行对应的函数
void instruct_run_handler(struct Nvm * vm, char * instruct, char * buffer);

#endif // !NVM_INSTRUCTION_H
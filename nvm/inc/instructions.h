#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

enum Instructions {
    /* 保存和加载的指令 */
    Instructions_IMM, /* load Immediate */
    Instructions_LEA,
    Instructions_LC,
    Instructions_LI,
    Instructions_SC,
    Instructions_SI,
    Instructions_PUSH,
    Instructions_POP,
    /* 运算 */
    Instructions_ADD,
    Instructions_SUB,
    Instructions_MUL,
    Instructions_DIV,
    Instructions_MOD,
    Instructions_OR,
    Instructions_XOR,
    Instructions_AND,
    Instructions_SHL,
    Instructions_SHR,
    Instructions_EQ,
    Instructions_NE,
    Instructions_LT,
    Instructions_LE,
    Instructions_GT,
    Instructions_GE,
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
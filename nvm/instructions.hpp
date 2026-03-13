#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <cstdint>
#include <map>
#include <regex>
#include <string>
#include <string_helper.hpp>
#include <vector>

#define DEBUG_INFO(fmt, ...)                                                             \
    printf("[info][%s][%d]:" fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

enum class NRegister {
    R0 = 0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
};

enum class NOpcode {
    LMM = 0, // 立即加载数据从内存到寄存器
    ST,      // 立即从寄存器保存数据到内存
    LEA,     // Load Effective Address，加载一个内存地址
    ADD,     // 加法
    SUB,     // 减法
    MUL,     // 乘法
    DIV,     // 除法
    MOD,     // 取余
    NOT,     // 取反
    AND,     // 按位与
    OR,      // 按位或
    XOR,     // 按位异或
    SHL,     // Shift Logical Left 逻辑左移
    SHR,     // Shift Logical Right 逻辑右移
    EQ,      // Equal 相等
    NE,      // Not Equal 不相等
    LT,      // Less Than 小于
    LE,      // Less Equal 小于等于
    GT,      // Greater Than 大于
    GE,      // Greater Equal 大于等于
    PUSH,    // 栈操作
    POP,
    JMP,  // 无条件跳转
    JIC,  // jump if condition，条件跳转，条件是根据状态寄存器来
    CALL, // 用于实现函数调用的指令
    RET,  // 用于实现函数调用返回的指令
    TRAP  // 产生trap
};

enum class NTrapType { GETCH, OUTCH, HALT };

class NInstructionsInterface
{
public:
    NInstructionsInterface() = default;
    virtual ~NInstructionsInterface() = default;
    // 指令二进制生成函数
    virtual std::vector<uint8_t> generateInstructionCode() = 0;
    // 获取指令名字
    virtual std::string generateInstructionName() = 0;
    // 解析指令文本，如果成功就会返回指令对应对象的指针，失败返回nullptr
    // virtual NInstructionsInterface* parserInstructionText(std::string text) = 0;
    // 获取opcode
    NOpcode getOpcode();
    // 设置opcode
    void setOpcode(NOpcode op);

private:
    NOpcode m_opcode;
};

class NInstructionsLMM : public NInstructionsInterface
{
public:
    NInstructionsLMM(std::string reg, int32_t val);
    ~NInstructionsLMM() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsST : public NInstructionsInterface
{
public:
    NInstructionsST(NRegister reg, int32_t val);
    ~NInstructionsST() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsLEA : public NInstructionsInterface
{
public:
    NInstructionsLEA(NRegister reg, int32_t val);
    ~NInstructionsLEA() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsADD : public NInstructionsInterface
{
public:
    NInstructionsADD(std::string reg, int32_t val);
    ~NInstructionsADD() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsSUB : public NInstructionsInterface
{
public:
    NInstructionsSUB(std::string reg, int32_t val);
    ~NInstructionsSUB() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsMUL : public NInstructionsInterface
{
public:
    NInstructionsMUL(NRegister reg, int32_t val);
    ~NInstructionsMUL() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsDIV : public NInstructionsInterface
{
public:
    NInstructionsDIV(NRegister reg, int32_t val);
    ~NInstructionsDIV() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsMOD : public NInstructionsInterface
{
public:
    NInstructionsMOD(NRegister reg, int32_t val);
    ~NInstructionsMOD() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsNOT : public NInstructionsInterface
{
public:
    NInstructionsNOT(NRegister reg);
    ~NInstructionsNOT() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

class NInstructionsAND : public NInstructionsInterface
{
public:
    NInstructionsAND(NRegister reg, int32_t val);
    ~NInstructionsAND() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsOR : public NInstructionsInterface
{
public:
    NInstructionsOR(NRegister reg, int32_t val);
    ~NInstructionsOR() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsXOR : public NInstructionsInterface
{
public:
    NInstructionsXOR(NRegister reg, int32_t val);
    ~NInstructionsXOR() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsSHL : public NInstructionsInterface
{
public:
    NInstructionsSHL(NRegister reg, int32_t val);
    ~NInstructionsSHL() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsSHR : public NInstructionsInterface
{
public:
    NInstructionsSHR(NRegister reg, int32_t val);
    ~NInstructionsSHR() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsEQ : public NInstructionsInterface
{
public:
    NInstructionsEQ(NRegister reg, int32_t val);
    ~NInstructionsEQ() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsNE : public NInstructionsInterface
{
public:
    NInstructionsNE(NRegister reg, int32_t val);
    ~NInstructionsNE() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsLT : public NInstructionsInterface
{
public:
    NInstructionsLT(NRegister reg, int32_t val);
    ~NInstructionsLT() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsLE : public NInstructionsInterface
{
public:
    NInstructionsLE(NRegister reg, int32_t val);
    ~NInstructionsLE() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsGT : public NInstructionsInterface
{
public:
    NInstructionsGT(NRegister reg, int32_t val);
    ~NInstructionsGT() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsGE : public NInstructionsInterface
{
public:
    NInstructionsGE(NRegister reg, int32_t val);
    ~NInstructionsGE() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
    int32_t m_val;
};

class NInstructionsPUSH : public NInstructionsInterface
{
public:
    NInstructionsPUSH(NRegister reg);
    ~NInstructionsPUSH() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

class NInstructionsPOP : public NInstructionsInterface
{
public:
    NInstructionsPOP(NRegister reg);
    ~NInstructionsPOP() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NRegister m_reg;
};

class NInstructionsJMP : public NInstructionsInterface
{
public:
    NInstructionsJMP(std::string target);
    ~NInstructionsJMP() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    std::string m_target;
};

class NInstructionsJIC : public NInstructionsInterface
{
public:
    NInstructionsJIC(std::string target);
    ~NInstructionsJIC() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    std::string m_target;
};

class NInstructionsCALL : public NInstructionsInterface
{
public:
    NInstructionsCALL(std::string target);
    ~NInstructionsCALL() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    std::string m_target;
};

class NInstructionsRET : public NInstructionsInterface
{
public:
    NInstructionsRET();
    ~NInstructionsRET() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
};

class NInstructionsTRAP : public NInstructionsInterface
{
public:
    NInstructionsTRAP(NTrapType type);
    ~NInstructionsTRAP() = default;
    // 指令二进制生成函数
    std::vector<uint8_t> generateInstructionCode() override;
    // 获取指令名字
    std::string generateInstructionName() override;
    // 从文本生成指令对象
    static NInstructionsInterface * parserInstructionText(std::string & text);

private:
    NTrapType m_type;
};

extern std::map<std::string, NRegister> g_textToRegisterMap;

extern std::map<std::string, NInstructionsInterface * (*)(std::string & text)>
  g_opcodeToGeneratorMap;

#endif // !NVM_INSTRUCTION_H

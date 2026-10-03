#ifndef NVM_CORE_H
#define NVM_CORE_H

#include "nvm/instructions.hpp"
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

constexpr int32_t DEFAULT_STACK_SIZE = 8 * 1024 * 1024;

// NCI v2.1 文件头尺寸与魔数（规范 §2）。与 nas 侧 nas/instruction.hpp 的
// NCI_HEADER_SIZE/NCI_MAGIC 同义异名——tests 会把两模块头文件收进同一 TU，
// 同名常量重定义冲突，命名须错开（先例见 DYNAMIC_IMPORT_FLAG）
constexpr int32_t NCI_V21_HEADER_SIZE = 32;
constexpr uint8_t NCI_V21_MAGIC[8] = { 'N', 'a', 'n', 'o', 'C', '\0', '\0', '\0' };

// 动态链接宿主地址分配起点（宿主地址与代码段地址空间隔离）。
// nas 对无地址 extern 分配的伪宿主地址区为 0x7E000000 起（DYNAMIC_HOST_BASE），
// 与本分配区隔离；两区均在代码/数据地址空间之外
constexpr int32_t HOST_ADDRESS_BASE = 0x7F000000;

// 调用约定（导入表 flags bit0-1）
constexpr int32_t CONV_FASTCALL = 0;
constexpr int32_t CONV_CDECL = 1;

// 导入表 flags bit2：动态导入标记（加载期按符号名经宿主库解析，规范 §2.1）。
// 与 nas 侧 IMPORT_FLAG_DYNAMIC（nas/instruction.hpp）同义异名，避免两模块
// 头文件在 tests 同 TU 内重定义冲突
constexpr int32_t DYNAMIC_IMPORT_FLAG = 0x4;

// NCI v2.1 导入符号（宿主函数引用）
struct NImportSymbol {
    std::string name; // 符号名（不含 NUL）
    int32_t addr;     // 宿主地址：静态绑定 / 伪地址（动态导入）/ 0（旧格式动态）
    int32_t flags;    // bit0-1 = 调用约定：0=fastcall，1=cdecl；bit2 = 动态导入
};

// NCI v2.1 导出符号（代码段地址）
struct NExportSymbol {
    std::string name; // 符号名（不含 NUL）
    int32_t addr;     // 代码段地址
    int32_t flags;    // 恒 0
};

// 宿主函数：regs = m_registers[8]（R4=SP），mem = m_stack 缓冲，memSize = 缓冲大小。
// 返回值由 VM 写入 R0
typedef int32_t (*NHostFunction)(int32_t * regs, int8_t * mem, int32_t memSize);

class NVirtualMachine
{
public:
    NVirtualMachine(int32_t stackSize = DEFAULT_STACK_SIZE);
    ~NVirtualMachine();

    void load(std::string filename);

    void start();

    void print_info();

    void print_stack(int32_t start, int32_t end);

    // 获取寄存器值
    int32_t getRegister(int32_t regIndex);

    // 设置寄存器值
    void setRegister(int32_t regIndex, int32_t value);

    // 获取PC值
    int32_t getPC();

    // 设置PC值
    void setPC(int32_t value);

    // 获取SP值
    int32_t getSP();

    // 设置SP值
    void setSP(int32_t value);

    // 获取BP值
    int32_t getBP();

    // 设置BP值
    void setBP(int32_t value);

    // 获取AX值
    int32_t getAX();

    // 设置AX值
    void setAX(int32_t value);

    // 获取flags值
    int32_t getFlags();

    // 设置flags值
    void setFlags(int32_t value);

    // 获取栈大小
    int32_t getStackSize();

    // 获取代码大小
    int64_t getCodeSize();

    // 获取数据段大小
    int32_t getDataSize();

    // 获取 v2.1 导入表
    const std::vector<NImportSymbol> & getImports();

    // 获取 v2.1 导出表
    const std::vector<NExportSymbol> & getExports();

    // 按地址注册宿主函数（对应导入表 addr != 0 的静态绑定；addr 须为非 0 正值）
    void registerHostFunction(int32_t addr, NHostFunction fn);

    // 按名注册宿主函数（供 resolveImportsByName 动态解析）
    void registerHostFunction(const std::string & name, NHostFunction fn);

    // 加载宿主动态库，解析导入表中的动态导入（flags bit2 置位的伪地址导入，或
    // 旧格式 addr == 0 导入）：按符号名 GetProcAddress/dlsym，命中后经签名包装器
    // 适配并登记在该导入的伪地址上（callx 站点 imm 天然命中；旧格式 addr=0 则从
    // HOST_ADDRESS_BASE 起分配并回填）。未命中 / 无已知签名包装器 → 明确报错
    // （Windows: LoadLibraryA + GetProcAddress；POSIX: dlopen + dlsym）
    bool loadHostLibrary(const std::string & path);

    // 用按名注册表解析导入表中的动态导入（判定同 loadHostLibrary），地址分配
    // 规则亦同（伪地址原位登记；addr=0 从 HOST_ADDRESS_BASE 起分配并回填）
    bool resolveImportsByName();

    // 获取栈指针
    int8_t * getStack();

    // 获取代码指针
    int8_t * getCode();

    // 指令执行
    void executeLMM();
    void executeST();
    void executeLEA();
    void executeLOAD();
    void executeSTORE();
    void executeLOADA();
    void executeSTOREA();
    void executeADD();
    void executeADDI();
    void executeSUB();
    void executeSUBI();
    void executeMUL();
    void executeMULI();
    void executeDIV();
    void executeDIVI();
    void executeMOD();
    void executeMODI();
    void executeNOT();
    void executeNEG();
    void executeCMP();
    void executeCMPI();
    void executeTEST();
    void executeAND();
    void executeOR();
    void executeXOR();
    void executeSHL();
    void executeSHR();
    void executeANDI();
    void executeORI();
    void executeXORI();
    void executeSHLI();
    void executeSHRI();
    void executePUSH();
    void executePUSHI();
    void executePOP();
    void executeENTER();
    void executeLEAVE();
    void executeJMP();
    void executeJZ();
    void executeJNZ();
    void executeJN();
    void executeJP();
    void executeCALL();
    void executeCALLX();
    void executeRET();
    void executeMOV();
    void executeCLR();
    void executeNOP();

private:
    // 严格 v2.1 加载路径：校验 32 字节头/两张表并载入数据段，失败抛 std::runtime_error
    void loadV21(const int8_t * data, int64_t fileSize);

    // 为符号分配（或复用）宿主地址并登记到 CALLX 分发表
    int32_t internHostSymbol(const std::string & name, NHostFunction fn);

    // 在指定宿主地址登记符号并返回登记地址：addr != 0（伪地址）原位登记，
    // callx 站点 imm 天然命中；addr == 0（旧格式动态导入）走 internHostSymbol
    // 从 HOST_ADDRESS_BASE 起分配
    int32_t bindHostSymbol(const std::string & name, NHostFunction fn, int32_t addr);

    // 由 CMP/CMPI/TEST 的结果置 flags：三态映射到 Z/N/P（互斥），其余位清零
    void setCompareFlags(int32_t result);

    // 导入符号是否为动态导入（待加载期解析）：旧格式 addr == 0 或 flags bit2
    static bool
    isDynamicImport(const NImportSymbol & sym)
    {
        return sym.addr == 0 || (sym.flags & DYNAMIC_IMPORT_FLAG) != 0;
    }

    int32_t m_pc;
    int32_t m_ax;
    int32_t m_flags;
    int32_t m_registers[REGISTER_COUNT];
    // 规范 v2.1 寄存器角色：R4=SP、R5=BP。用引用别名保证寄存器指令
    // （mov/addi R4,...）与栈指令操作同一份存储
    int32_t & m_sp;
    int32_t & m_bp;
    int8_t * m_stack;
    int8_t * m_code;
    int32_t m_stackSize;
    int64_t m_codeSize;
    int32_t m_dataSize;
    std::vector<NImportSymbol> m_imports;
    std::vector<NExportSymbol> m_exports;
    // CALLX 分发表：宿主地址 → C 函数
    std::map<int32_t, NHostFunction> m_hostFunctions;
    // 按名注册表（registerHostFunction(name, fn)）
    std::map<std::string, NHostFunction> m_hostFunctionsByName;
    // 已分配宿主地址的符号名（避免同一符号重复分配）
    std::map<std::string, int32_t> m_hostAddrByName;
    // 下一个可分配的宿主地址，从 HOST_ADDRESS_BASE 起递增
    int32_t m_nextHostAddr;
};

#endif // !NVM_CORE_H

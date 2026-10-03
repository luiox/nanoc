#ifndef NAS_LINKER_H
#define NAS_LINKER_H

#include <cstdint>
#include <string>
#include <vector>

// 链接结果：ok=false 时 errorMessage 有效
struct LinkResult {
    bool ok = false;
    std::vector<uint8_t> image; // 完整 NCI v2.1 可执行文件镜像
    std::string errorMessage;
};

// NCI v2.1 目标文件链接器（PRD R7）：N 个 .nci 对象 → 1 个可执行 .nci。
// 布局与符号表字节图见 doc/Bytecode Format Specification v2.1.md §2/§2.1，
// 链接语义记录于规范 §2.2 与本注释。
//
// 链接算法（输入模块顺序 = 命令行顺序）：
//   1. 段合并：code/data 顺序拼接。统一编址下模块 i 的代码基址 = Σ前面 codeSize，
//      数据基址 = Σ前面 (codeSize+dataSize)
//   2. 重定位：按规范 §3.1 指令长度表线性解码 code 段，地址类指令 imm 做范围判断：
//      < 模块 codeSize → 代码地址（+代码基址）；∈ [codeSize, codeSize+dataSize) →
//      数据地址（+数据基址）；≥ codeSize+dataSize → 宿主地址（不动）。
//      CALLX 不做范围平移（见下）
//   3. 符号解析（内部定义优先）：导入 addr=0（动态）且名字命中任一模块导出 →
//      内部解析：该模块内 imm 与导入 addr 相等的 CALLX/LEA/LOADA/STOREA/ST 站点
//      改写为平移后的目标地址，导入从输出表移除；其余导入去重合并
//      （重名 flags 冲突报错），addr 原样保留（宿主静态地址或 0 = 动态）
//   4. 导出表：各模块导出地址平移后合并（重名报错）；entryPoint：任一模块导出
//      main → 用之，否则第一个导出符号，否则 0
//
// 决策与已知限制（NCI v2.1 无逐字重定位信息）：
//   - CALLX imm 按值匹配导入表（汇编器以导入 addr 回填 callx 站点）；仅 addr=0 的
//     动态导入可被内部解析改写，显式静态宿主绑定（addr≠0）不改写
//   - 同一模块多个 addr=0 导入中存在可内部解析者时，callx 站点无法映射到具体
//     符号 → 报错（避免静默错链）；宿主/内部混用需为 extern 指定显式地址
//   - 数据段不做扫描重定位：dd 地址常量链接后失效（范围启发式会破坏零初始化
//     全局量与字符串数据；ncc 现仅产出 dd 0，不受影响）。跨模块数据引用统一走
//     "导出数据标号 + 导入解析"路径（指令 imm 重定位）
//   - 输入文件的 entryPoint 不保留，输出按导出表重算
class Linker
{
public:
    // 链接多个 .nci 目标文件（从磁盘读取）
    static LinkResult linkFiles(const std::vector<std::string> & inputPaths);

    // 链接多个内存中的 .nci 镜像（测试入口）
    static LinkResult linkImages(const std::vector<std::vector<uint8_t>> & images);
};

#endif // NAS_LINKER_H

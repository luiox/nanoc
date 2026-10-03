#ifndef NCC_PREPROCESSOR_H
#define NCC_PREPROCESSOR_H

#include "ncc/semantic.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// 预处理产物：一个文件的行对齐预处理文本（PRD R9 include C 头文件·声明子集）。
// - 行对齐：输出第 i 行对应源文件第 i 行（指令行清空、enum 定义原位改写为
//   `int`），解析诊断的行列与原文件一致。
// - isHeader：头文件产物。装载器据此把声明 sourceFile 置空（全编译单元可见，
//   与 R7 合成声明同口径）并让 Parser 进入头文件模式（R9 声明子集扩展）。
struct PrepFile {
    std::string canonical; // 规范绝对路径（幂等/诊断键）
    std::string display;   // 显示路径（诊断前缀、依赖清单）
    bool isHeader = false;
    std::string text; // 预处理后文本（与源文件行对齐）
};

// R9 行级预处理器。一个编译单元一个实例：常量表与已处理头文件集跨文件共享
// （头文件每单元只展开一次，声明不重复拼接）。
//
// 支持与不做（决策记录，PRD R9；"预处理器是无底洞，宁少勿滥"）：
// - `#include "rel/path.h"`：引号形式、相对当前文件目录解析、目标必须 .h；
//   `<...>` 形式报"不支持"（系统头不在子集内）。
// - 幂等：以规范绝对路径 memo，重复 include 静默跳过；循环 include 同样跳过
//   （进入即标记，与 C guard 在首次处理时即生效的口径一致）——不报错。
// - include guard 识别：文件指令序列为 `#ifndef G`（后随可选 `#pragma once`）
//   + `#define G`（同名）+ ... + `#endif`（末条指令、中间无条件指令）时按
//   guard 剥离这三行（guard 宏不进常量表）；其余 #if/#ifdef/#ifndef/#elif/
//   #else/#endif 一律报"条件编译不支持"。
// - `#pragma once` 标记文件已处理；其余 #pragma 报不支持（#pragma pack 等
//   影响布局语义，静默忽略会误编译）。
// - `#define` 仅对象宏：`#define NAME 值`，值为文本替换（登记时按当前常量表
//   做单层展开，不求值）；无值视为替换为空；NAME 紧贴 `(` 为函数宏 → 报
//   "不支持"；NAME 为 NanoC 关键字（int/NULL 等）时忽略该行（语言关键字
//   不可被文本替换，静默吞掉比报错更接近 C 的无害重定义）。
// - enum（仅头文件内改写）：`enum [Tag] { ... }` 定义原位改写为 `int`，枚举器
//   按整型常量登记（= 值支持 整数/已知常量/+ - * / % 括号/一元 -，十进制）；
//   `enum Tag` 类型引用改写为 `int`（C 枚举即 int 尺寸）。语言本体不新增
//   enum 语法，.nc 文件中的 enum 不改写（解析期自然报错）。
// - 固定宽度类型预置表：int8_t/int16_t/int32_t/uint8_t/.../int64_t/uint64_t/
//   size_t/ptrdiff_t/intptr_t/bool/_Bool 预置为 `int`，true/false 预置为
//   `1`/`0`（VM 字宽 32 位，64 位类型折叠为 int——决策记录）。
// - 不支持（识别后报错而非误编译）：#undef/#error/#line/#（空指令）/未知指令；
//   #define 续行 `\`；函数宏；浮点类型（float/double，由 Parser 头文件模式报）。
//
// 常量替换范围：本单元中定义点之后的全部非指令文本（含后续 include 的头文件
// 与包含者剩余文本）；只替换完整标识符，跳过字符串/字符字面量与注释；单层
// 展开（替换文本不再重扫）。
class Preprocessor {
public:
    // 处理一个文件：产出 DFS 首现序的 PrepFile 列表——被 include 的头文件段
    // 先于本文件段（递归展开），头文件声明在合并单元中先于包含者。
    // 返回 false = 出现错误诊断（已追加到 diagnostics，调用方终止装载）。
    bool process(const std::filesystem::path& path,
                 const std::string& canonical,
                 const std::string& display,
                 bool isHeader,
                 std::vector<PrepFile>& out,
                 std::vector<Diagnostic>& diagnostics);

private:
    bool processFile(const std::filesystem::path& path,
                     const std::string& canonical,
                     const std::string& display,
                     bool isHeader,
                     std::vector<PrepFile>& out,
                     std::vector<Diagnostic>& diagnostics);

    // 固定宽度类型/bool 预置常量（每单元一次）
    void seedPresetConstants();

    // ---- 指令识别 ----
    struct Directive {
        int index = 0;       // 行号（0 起）
        std::string name;    // 指令名（include/define/pragma/...）
        std::string rest;    // 指令名后的剩余文本（已去首尾空白）
        std::size_t col = 0; // '#' 列（1 起，诊断用）
    };
    // 行首（首个非空白字符）为 '#' 时解析为指令
    static bool scanDirective(const std::string& line, Directive& directive);
    // include guard 识别：返回需剥离的行号集合（#ifndef/#define/#endif 三行）
    std::set<int> detectGuardLines(const std::vector<Directive>& directives) const;

    // ---- 常量替换（注释/字符串感知，标识符整词匹配）----
    // 把 src 中已登记常量的标识符替换为常量值，追加到 out
    void expandText(const std::string& src, std::string& out) const;
    // 单行便捷形式
    std::string expandLine(const std::string& line) const;

    // ---- enum 改写 ----
    // 在 text[i..] 遇到整词 `enum`（仅头文件模式调用）：定义形态改写为 `int`
    // 并登记枚举器常量；类型引用形态改写为 `int`。out 追加改写结果，i 推进
    // 到 span 之后；span 内换行原样保留并同步 lineCounter（行对齐）。
    // 失败时追加诊断并返回 false。
    bool rewriteEnum(const std::string& text,
                     std::size_t& i,
                     std::string& out,
                     const std::string& display,
                     int& lineCounter,
                     std::vector<Diagnostic>& diagnostics);

    // 枚举体解析：逗号分隔的 `NAME [= EXPR]`，登记为整型常量（隐式值 = 前值+1）
    bool registerEnumerators(const std::string& body,
                             const std::string& display,
                             int lineNumber,
                             std::vector<Diagnostic>& diagnostics);

    // 常量表达式求值（枚举器 = 值）：十进制整数、+ - * / %、括号、一元 -；
    // 标识符先经 expandText 展开，残留标识符/不支持运算符报错
    bool evalConstExpr(const std::string& text, int& out) const;

    // ---- 工具 ----
    void reportError(const std::string& display,
                     int line,
                     int col,
                     const std::string& message,
                     std::vector<Diagnostic>& diagnostics) const;
    static bool isIdentChar(char c);
    static bool isIdentStart(char c);
    static std::string trim(const std::string& text);
    static std::vector<std::string> splitLines(const std::string& source);
    static bool matchWord(const std::string& text, std::size_t pos, std::string& word);

    std::map<std::string, std::string> m_constants; // 常量表（宏 + 枚举器 + 预置）
    std::set<std::string> m_processed;              // 已处理头文件规范路径（幂等 memo）
    bool m_seeded = false;                          // 预置常量已播种
};

#endif // NCC_PREPROCESSOR_H

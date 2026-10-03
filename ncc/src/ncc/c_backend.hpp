#ifndef NCC_C_BACKEND_H
#define NCC_C_BACKEND_H

#include "ncc/ir.hpp"

#include <string>

// C 后端（PRD R4 / M2）：ir::Module → 可读 C。定位为差分测试 oracle（R13 基准）
// 与 xmake rule("nanoc") 一期流水线的"前端预处理器"产物（R5）。
//
// 输入契约与 lower 一致：已通过 SemanticAnalyzer 的单 Program 降级出的单
// Module（多文件 import/export 归并后的单编译单元）；类型层面的非法输入已
// 在语义层报错，此处不重复诊断，仅对防御性分支（Error 类型）做兜底映射。
//
// ---------------------------------------------------------------------------
// 类型映射表（NanoC → C）
// ---------------------------------------------------------------------------
// | NanoC        | C                       | 说明                               |
// |--------------|-------------------------|------------------------------------|
// | int          | int32_t                 | 固定宽度，含 <stdint.h>            |
// | char         | char                    | 决策见下                           |
// | void         | void                    |                                    |
// | T*           | T*                      | C 指针本征指针算术/下标语义        |
// | struct Tag   | struct Tag              | 同名标签，按声明序发射             |
// | T[N]         | T name[N]               | 一维数组，声明符位置拼接 [N]       |
// | 字符串字面量 | static const char NcStrK[] | 文件级去重池，引用点用池名       |
// | null         | NULL                    | <stddef.h>                         |
//
// char → char（偏离 PRD 草案的 int8_t，已裁决）：C 标准库字符串接口
// （puts/printf/strlen）要求 char*，int8_t 与 char 在 ABI 上同型但类型不同，
// 现代 clang（15+）对不兼容指针类型默认按错误处理；char 在 MSVC/x64 与
// Linux x64 上默认均为 signed，与 int8_t 数值语义一致。
//
// ---------------------------------------------------------------------------
// struct 布局保证（PR #44：全成员 4 字节对齐、无填充）
// ---------------------------------------------------------------------------
// 方案二选一裁决：选 _Static_assert(offsetof/sizeof)，弃 #pragma pack。
// pack(push,4) 会让 8 字节的 C 指针成员与后续成员存储重叠（pack 把尾部成员
// 排进指针的高 4 字节），语义错误；offsetof 断言则把布局契约钉进生成代码。
// 后端按 NanoC 规则（int/char/T* 均 4 字节、偏移 = 累计和、无填充）与
// C 映射规则（int32_t=4、char=1、T*=8@x64）分别计算成员偏移：
// - 两侧一致（全 int 成员、char 成员经 C 自然补齐落位相同等场景）：
//   逐成员发射 _Static_assert(offsetof(S,m)==NanoC 偏移) + sizeof 断言，
//   布局分歧在 C 编译期报错；
// - 两侧不一致（指针成员按 8 对齐、char 数组按 1 字节尺寸累积——mandated
//   映射下无法复现 VM 字节布局）：不发射断言，留注释说明；生成的 C 自身
//   自洽（同一结构内偏移/尺寸由同一编译器唯一决定），差分验收不受影响。
// 跨 ABI 字节级互操作（R9 include C 头）以真实 C 布局为权威，届时再裁。
//
// ---------------------------------------------------------------------------
// 其余语义决策
// ---------------------------------------------------------------------------
// - 控制流/短路：if/while/for/break/continue 一比一结构化发射；&&/|| 直接
//   映射 C 本征短路（VM 侧需分支实现，C 侧免费获得，差分可验证短路次序）。
// - fastcall 前 4 参数寄存器是 VM 概念，C 后端普通传参。
// - sret：struct 按值返回（struct Tag f()），sret 细节交平台 C ABI（与 VM 的
//   显式 R7 机制语义等价，C 侧以 C ABI 为权威）。
// - struct 赋值/传参/返回均为值语义，C 结构体赋值天然同型。
// - 未初始化局部变量：按源码原样发射（C 为不确定值；VM 内存零清零）——
//   有效 NanoC 程序不读未初始化值，差分程序需全量初始化。
// - 链接性：函数/全局用外部链接（R7 独立编译链接交系统链接器，多模块
//   符号互见）；字符串池 static（纯内部实现细节）。
// - extern 原型：module.externs（PRD R3）按声明发射带签名 C 原型
//   `extern <ret> name(params);`，varargs 尾部发射 `...`；仅当调用点 callee
//   既无定义又无 extern 声明时（跳过语义门禁的降级输入）沿用无参原型
//   `extern <ret> name();`（#37 兼容路径）；返回类型未知时兜底 int32_t。
//
// 输出形态：结构化缩进（4 空格、K&R）、块语句加大括号、函数/结构体带
// `// func:` / `// struct:` 行注释保留 NanoC 源类型拼写（溯源到 IR dump）。
namespace c_backend {

    // 将 IR 模块发射为一份自洽可编译的 C 翻译单元文本
    std::string emit(const ir::Module& module);

} // namespace c_backend

#endif // NCC_C_BACKEND_H

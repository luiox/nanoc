#ifndef NCC_LLVM_BACKEND_H
#define NCC_LLVM_BACKEND_H

#include "ncc/ir.hpp"

#include <string>

// LLVM 后端（PRD R6 / M3）：ir::Module → LLVM 文本 IR（.ll）。与 C 后端
// （c_backend.hpp，最佳参照）同构：同样消费 ir::Module、同样的输入契约
// （已通过 SemanticAnalyzer 的单 Program 降级出的单编译单元；类型层面的非法
// 输入已在语义层报错，此处仅对防御性分支（Error 类型）做兜底映射）。
//
// ---------------------------------------------------------------------------
// 类型映射表（NanoC → LLVM）
// ---------------------------------------------------------------------------
// | NanoC        | LLVM                   | 说明                               |
// |--------------|------------------------|------------------------------------|
// | int          | i32                    | 4 字节有符号                       |
// | char         | i8                     | 决策见下                           |
// | void         | void                   |                                    |
// | T*           | ptr                    | opaque pointers，决策见下          |
// | struct Tag   | %struct.Tag            | named struct，按声明序发射         |
// | T[N]         | [N x T']               | T' 为元素映射类型                  |
// | 字符串字面量 | @NcStrK（private       | 与 C 后端同名的文件级去重池        |
// |              | constant [N x i8]）    |                                    |
// | null         | null（ptr）            |                                    |
//
// char → i8（与 C 后端 char → char 对齐的决策）：clang 前端正是把 C char
// 映射到 i8；C 后端的 char 语义（MSVC/x64 与 Linux x64 均默认 signed）在
// LLVM 侧对应"i8 存储 + 有符号扩展提升"：读值 sext i8→i32、写值 trunc
// i32→i8、比较/算术一律在 i32 域按有符号（icmp slt 等 / sdiv / srem）——
// 与 C 的整型提升逐点一致。字符串字面量与 char 数组自然落在 [N x i8]，
// char* 与字面量指针类型贯通（都是 ptr）。
//
// opaque pointers（LLVM 15+ 默认、14 可选）：全部指针写 `ptr`，不生成
// typed pointer；本机与 CI 的 LLVM 版本可能不同，opaque 风格 .ll 在 15+
// 均可汇编。gep 全部携带显式元素类型（`getelementptr i32, ptr ...`）。
//
// ---------------------------------------------------------------------------
// struct 布局（对齐 C 后端的布局决策口径）
// ---------------------------------------------------------------------------
// C 后端的裁决是：自然布局 + 布局可复现时 _Static_assert 钉死、不可复现时
// 留注释。LLVM 侧同理取自然布局（i32=4B/4B、i8=1B/1B、ptr=8B/8B，成员偏移
// 按自然对齐、总尺寸补齐），gep 按成员序号访问；NanoC 全 4 字节无填充布局
// 与自然布局一致时（全 int 成员等）发射"布局匹配"注释，不一致时（char/指针
// 成员）发射 layout note 注释——布局断言只在 VM/C 差分层有意义，LLVM 侧
// 以同一编译器口径自洽为权威，跨 ABI 字节级互操作（R9）届时再裁。
//
// ---------------------------------------------------------------------------
// 其余语义决策
// ---------------------------------------------------------------------------
// - 控制流：if/while/for → 基本块 + br/条件 br；短路 &&/|| 块分裂 + phi
//   归并（i1），值语境 zext 到 i32。break/continue 跳最近循环的
//   end/continue 目标（for 的 continue 目标是 step 块）。
// - 下标/成员/取址/解引用 → gep/load/store；函数参数与局部变量一律
//   alloca + store（mem2reg 交给 LLVM opt，后端不做 SSA 化）；所有 alloca
//   提升到入口块（避免循环体内声明反复吃栈）。
// - 赋值/传参/返回的 char 收窄与提升按 C 规则（表达式值域 i32，落 i8
//   lvalue 时 trunc）。
// - struct 按值赋值/传参/返回 = 聚合 load/store（平台 ABI 由 llc 后端
//   下降，与 VM 显式 sret 机制语义等价）。
// - 指针算术：p ± i → 按元素尺寸 gep；p - q → ptrtoint/sub/sdiv（元素
//   尺寸缩放，C 语义）。
// - 全局初始化：常量初始化器（int/char/null/字符串字面量、struct 逐成员
//   常量、一元负号）直接写入 global 初始化；非常量初始化器对齐 VM 语义
//   （启动时先跑全局初始化再进 main）改为在 main 入口前注入初始化代码，
//   无 main 时报错（C 后端此场景生成非法 C，属既有已知分歧，LLVM 侧取
//   超集行为并记录）。
// - extern 原型（PRD R3）：按声明发射 `declare`，varargs 尾部 `...`；
//   varargs 实参保持 i32 提升（C 缺省提升），固定参数按声明类型收窄。
//   未解析外部（跳过语义门禁的降级输入，#37 兼容路径）：llc 不接受调用点
//   隐式声明，发射零固定参数的 varargs declare（承接任意调用形状），返回
//   类型兜底 i32（同 C 后端口径）。
// - 目标三元组：不发射 target triple，llc 用宿主默认（本机与 CI 同为
//   x86_64；注释钉死该决策，交叉编译需求出现时再显式化）。
// - 链接性：函数/全局默认外部链接（R7 独立编译交系统链接器）；字符串池
//   private。
//
// ---------------------------------------------------------------------------
// 工具链探测与 --emit=obj/exe（PRD R6）
// ---------------------------------------------------------------------------
// 探测顺序：环境变量 NANOC_LLVM_DIR（指向 LLVM bin 或其根目录）> PATH 上的
// llc > 本机参考 SDK 路径（新版优先）。探测结果进程内缓存；llc 用于
// .ll → .obj（-filetype=obj），exe 链接器依次探测 lld-link（自动发现 MSVC
// 安装）与 clang 驱动（同目录回退）。无工具链时给出含安装指引的报错。
namespace llvm_backend {

    // 将 IR 模块发射为一份自洽可汇编的 LLVM 文本 IR（opaque pointer 风格）
    std::string emit(const ir::Module& module);

    // -----------------------------------------------------------------------
    // 工具链探测结果（进程内缓存）
    // -----------------------------------------------------------------------
    struct Toolchain {
        std::string llc;    // llc 命令（绝对路径或 PATH 名）；空 = 不可用
        std::string linker; // 首选链接命令（lld-link 或 clang 驱动）；空 = 不可用
        std::string source; // 探测来源描述（日志/报错用）
    };

    // 探测 LLVM 工具链（llc + 链接器）；结果缓存，重复调用无副作用
    const Toolchain& toolchain();

    // .ll 文本 → 目标文件（需要 llc）。失败返回 false 并填充诊断
    // （含命令行与工具日志）。临时 .ll 落在目标文件旁，用后即删。
    bool compileToObj(const std::string& llText,
                      const std::string& objPath,
                      std::string& diagnostics);

    // .ll 文本 → 可执行文件（llc + 链接器）。失败返回 false 并填充诊断。
    bool compileToExe(const std::string& llText,
                      const std::string& exePath,
                      std::string& diagnostics);

} // namespace llvm_backend

#endif // NCC_LLVM_BACKEND_H

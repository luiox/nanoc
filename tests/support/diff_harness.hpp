#ifndef NANOC_TESTS_SUPPORT_DIFF_HARNESS_H
#define NANOC_TESTS_SUPPORT_DIFF_HARNESS_H

// R13 差分测试框架·共用设施（PRD：把散落的差分断言收敛成可扩展矩阵）。
//
// 提供两类能力：
// 1. 单后端执行器：runOnVm（进程内，复用 golden e2e 的 codegen→nas→nvm 链路）
//    与 runOnC（进程外真编译：c_backend::emit → C 编译器 → 运行）。
// 2. 后端抽象与矩阵驱动：IDiffBackend 探针 + 执行器 + 注册表，runRow 按程序
//    逐后端执行并产出 pass/skip/fail 单元，renderMatrix 渲染可读矩阵。
//
// ---------------------------------------------------------------------------
// 退出码映射口径（矩阵断言的唯一可比观测）
// ---------------------------------------------------------------------------
// - VM 后端：main 返回值落在 R0（int32_t），无内建输出（无系统调用）。
// - C 后端：`int32_t main(void)` 的返回值经 C 运行时交还给进程退出码。
//   Windows 退出码保留 32 位全宽；POSIX 仅保留低 8 位。为了跨平台口径一致，
//   矩阵统一按低 8 位归一化比较：`vmR0 & 0xFF == cExitCode & 0xFF`。
// - 255 截断边界：R0 = 256 与 R0 = 0 在该口径下同余不可区分；矩阵程序约定
//   main 返回值落在 [0, 255]（或显式接受 mod 256 同余），使映射保持单射。
//   负值按补码截断（-1 → 255），两侧口径一致。
// - 当前 VM 无 stdout（TODO(R3 后续)：宿主调用落地后把 stdOut 纳入比较口径，
//   并把 extern/puts 场景加入矩阵，见文件尾部 TODO 注释）。
//
// ---------------------------------------------------------------------------
// R6 接入点（未来 LLVM 后端如何加入矩阵）
// ---------------------------------------------------------------------------
// 1. 实现 `LlvmBackend : public nanoc_diff::IDiffBackend`：
//    - name() 返回 "llvm"（矩阵列名）；
//    - probe() 探测 llc/clang 工具链可用性（不可用返回 false → 矩阵记 skip，
//      不算失败，与 C 后端的 skip 策略一致）；
//    - execute() 走 ir::Module → LLVM IR → 目标可执行 → 运行 → 退出码，并把
//      退出码归一化到低 8 位填入 BackendOutput::exitCode8。
// 2. 在 registerBuiltinBackends() 里追加一次 registerBackend(...)（或由测试
//    侧自行注册），矩阵与汇总渲染对后端数量无假设，自动扩列。

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nanoc_diff {

    // ---------------------------------------------------------------------------
    // 单后端执行结果（进程内 / 进程外各自的原始口径）
    // ---------------------------------------------------------------------------

    // VM 后端（进程内）：nccSource → codegen → nas → nvm，全链路无外部进程
    struct VmResult {
        bool ok = false;         // 前端/汇编/执行全部成功且正常停机
        int32_t r0 = 0;          // 停机时 R0（main 返回值）
        std::string stdOut;      // VM 无内建输出，恒为空（留作未来 syscall 扩展）
        std::string diagnostics; // 失败时的阶段与原因描述
    };

    // C 后端（进程外）：nccSource → c_backend::emit → 落盘 → C 编译器 → 运行
    struct CResult {
        bool ok = false;         // 含编译在内的完整执行成功
        int exitCode = 0;        // 被测程序进程退出码（std::system 原始值）
        std::string diagnostics; // 编译失败时含命令行、编译日志与生成的 C 文本
    };

    // VM 侧全链路执行；正常停机（PC 哨兵/栈/帧复原，同 golden e2e 口径）才算 ok
    VmResult runOnVm(const std::string& nccSource);

    // C 侧真编译运行。tempBase 为临时文件基名（<tempBase>.c/".exe"/".log"，落在
    // CWD，用后即删）；编译器探测见 cCompilerCommand()。无编译器时 ok=false 且
    // diagnostics 说明——调用方应先经 probe()/cCompilerAvailable() 记 skip。
    CResult runOnC(const std::string& nccSource, const std::string& tempBase);

    // C 编译器探测（与 #47 策略对齐，进程内缓存）：
    // 环境变量 NANOC_C_COMPILER > PATH 上的 clang > gcc；均不可用返回空串。
    const std::string& cCompilerCommand();
    bool cCompilerAvailable();

    // ---------------------------------------------------------------------------
    // 后端抽象（矩阵驱动口径；R6 接入点见文件头注释）
    // ---------------------------------------------------------------------------

    struct BackendOutput {
        bool executed = false; // false：内部错误（前端/汇编/编译失败），看 diagnostics
        int rawExit = 0;       // 原始观测（VM = R0；C = 进程退出码全宽值）
        int exitCode8 = 0;     // 归一化退出码（rawExit & 0xFF，矩阵比较口径）
        std::string diagnostics;
    };

    class IDiffBackend {
    public:
        virtual ~IDiffBackend() = default;

        // 矩阵列名（如 "vm" / "c" / 未来的 "llvm"）
        virtual std::string name() const = 0;

        // 可用性探测：false → 该后端整列记 skip 而非 fail（PRD R13 skip 策略）
        virtual bool probe() = 0;

        // 执行一个 NanoC 程序并产出归一化观测
        virtual BackendOutput execute(const std::string& nccSource) = 0;
    };

    // 注册表（进程内全局；保持注册序，矩阵按注册序扩列）
    void registerBackend(std::unique_ptr<IDiffBackend> backend);
    const std::vector<IDiffBackend*>& backends();

    // 注册内置两后端：vm（进程内，恒可用）与 c（进程外真编译，随编译器探测）。
    // 幂等：重复调用不产生重复列。未来 LLVM 后端在此追加（见文件头 R6 接入点）。
    void registerBuiltinBackends();

    // ---------------------------------------------------------------------------
    // 矩阵驱动：程序集 × 后端
    // ---------------------------------------------------------------------------

    enum class CellStatus { Pass, Fail, Skip };

    struct MatrixCell {
        std::string program; // 程序展示名（矩阵行名）
        std::string backend; // 后端列名
        CellStatus status = CellStatus::Skip;
        int rawExit = 0;    // 仅 Pass 有效：原始观测（VM = R0；C = 全宽退出码）
        int exitCode8 = 0;  // 仅 Pass 有效：rawExit & 0xFF
        std::string detail; // Fail 时的诊断详情；Skip 时的原因
    };

    struct DiffProgram {
        std::string name;                  // 展示名（如 "examples/hello.nc"）
        std::string slug;                  // 临时文件基名片段（仅 [A-Za-z0-9_]）
        std::string source;                // NanoC 源码全文
        std::optional<int32_t> expectedR0; // 已知锚点值（防两后端同错；可空）
    };

    // 运行矩阵一行：程序 × 全部已注册后端
    std::vector<MatrixCell> runRow(const DiffProgram& program);

    // 行内一致性：全部 Pass 单元的 exitCode8 一致时返回空串；否则返回形如
    // "c exit8=12 (raw 12) != vm exit8=13 (raw 13)" 的差异描述（第一列 vm 为基准）
    std::string rowMismatch(const std::vector<MatrixCell>& cells);

    // 渲染可读矩阵（每程序×后端 → pass(退出码)/SKIP/FAIL），含图例；用于测试日志
    std::string renderMatrix(const std::vector<MatrixCell>& cells);

    // 从 CWD 逐级向上定位并读入 examples/<name>（同 test_golden_e2e.cpp 策略）；
    // 找不到返回 false（由调用方决定 FAIL 或 SKIP）
    bool readExampleSource(const std::string& name, std::string& out);

} // namespace nanoc_diff

// TODO(R3 后续)：extern/宿主调用（puts 等）场景本期不入矩阵——VM 侧尚无宿主
// syscall 注入（stdOut 恒空），C 侧宿主桩参考 test_c_backend.cpp 的
// ExternCallSmoke。R3（代理A：extern 声明 + --emit=c 接线）合并后下一轮：
// 1. 给 DiffProgram 增加"宿主桩"字段（C 侧追加链接的定义 / VM 侧注入的宿主
//    函数表）；2. 比较口径从 exitCode8 扩展为 {exitCode8, stdOut}；3. 新增
//    puts/nc_add 等程序行。

#endif // NANOC_TESTS_SUPPORT_DIFF_HARNESS_H

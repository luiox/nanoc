#ifndef NCC_LOADER_H
#define NCC_LOADER_H

#include "ncc/ast.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
#include "ncc/preprocessor.hpp"
#include "ncc/semantic.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// 多文件装载结果（PRD R2a）：
// - ok：装载（含解析）是否成功；失败时 diagnostics 给出全部错误
// - program：成功时的合并编译单元——所有文件的声明按装载顺序拼接，
//   每条 Decl::sourceFile 已标注定义所在文件（语义/代码生成的可见性与
//   标号 mangle 依据）
// - loadOrder：实际装载的文件（显示路径，含入口），供依赖文件（-MMD）输出
struct LoadResult {
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
    std::unique_ptr<Program> program;
    std::vector<std::string> loadOrder;
};

// 依赖模块导出符号签名（PRD R7 轻装载）：从 import 闭包内各模块的顶层
// export 声明提取的纯数据视图。函数签名覆盖返回类型与参数表（供合成 extern
// 声明做调用点类型检查）；全局变量签名覆盖声明形态（供合成导入声明）。
// struct/typedef 不在本期签名范围（类型命名空间不做跨模块分级，见类注释）。
struct ImportedSymbol {
    std::string name;
    std::string module; // 定义模块显示路径（相对入口目录）
    bool isFunction = false;
    int line = 0; // 定义位置（诊断定位用）
    int column = 0;

    // ---- 函数签名（isFunction = true）----
    std::string returnType;
    bool returnIsStruct = false;
    int returnPointerDepth = 0;
    bool isVariadic = false;
    struct Param {
        std::string name;
        std::string type;
        bool isStructTag = false;
        int pointerDepth = 0;
    };
    std::vector<Param> params;

    // ---- 全局变量签名（isFunction = false）----
    std::string type;
    bool isStructTag = false;
    int pointerDepth = 0;
    bool isArray = false;
    int arraySize = 0;
    int arrayDims = 0;
};

// 独立编译装载结果（PRD R7）：
// - ok/diagnostics：与 LoadResult 同口径
// - program：单模块编译单元 = 入口模块自身声明 + 前置注入的依赖签名合成
//   声明（函数 → isExtern 声明走既有 PRD R3 检查与 callx 发射；全局变量 →
//   isImported 声明走导入表引用）。依赖声明在前与整体编译的 DFS 先序一致
//   （"先声明后使用"的全局变量可见顺序）
// - loadOrder：依赖闭包 + 入口（depfile 增量追踪，口径与 load() 一致）
// - imports：依赖导出签名集（调用方测试/诊断用；合成声明已含同等信息）
struct StandaloneResult {
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
    std::unique_ptr<Program> program;
    std::vector<std::string> loadOrder;
    std::vector<ImportedSymbol> imports;
};

// 多文件装载器（PRD R2a「文件即模块」，一期整体编译；PRD R7 增加独立编译
// 轻装载 loadStandalone）。
//
// 装载算法（决策记录）：
// - 以每个入口文件为根做深度优先装载；import 语法上位于文件顶部，因此
//   「先递归装载全部 import 目标、再拼接本文件声明」与文本顺序一致，
//   合并单元 = DFS 先序声明流（导入模块的声明先于导入者自身声明，
//   与 C 先 include 后使用的全局变量可见顺序一致）。
// - 以规范绝对路径做 memo：重复 import 幂等，声明只在首次 import 处
//   拼接一次（重复 import 不报错、不重复登记符号）。
// - 环形检测：DFS 活动栈命中目标文件即报错，诊断定位到闭环的 import
//   语句，消息含完整链（如 main.nc -> a.nc -> b.nc -> a.nc）。
//
// import 路径解析（相对导入者文件目录）：
// - `import math;` → <导入者目录>/math.nc
// - `import "util/helpers.nc";` → <导入者目录>/util/helpers.nc
// - 带引号路径若不带 .nc 后缀且按原文找不到，追加 .nc 再试一次
//
// 显示路径：诊断与 import 链统一用相对入口目录的路径（同目录工程呈现为
// a.nc -> b.nc -> a.nc）；无法相对化时回退规范化绝对路径。
// 已知限制（一期不做）：路径大小写不敏感的文件系统上，仅大小写不同的
// 两种拼写不视为同一文件（环形检测与幂等以字符串相等为准）。
//
// include C 头文件（PRD R9）：每个文件先经 Preprocessor 行级预处理——
// `#include "rel/path.h"`（引号形式、相对当前文件）递归展开，头文件段在
// 合并单元中先于包含者声明（首次 include 处，DFS 首现序）；头文件以规范
// 绝对路径 memo，重复/循环 include 幂等跳过（头文件声明每单元只拼接一次，
// 跨 .nc 共享同一份——struct/typedef/原型不因多处 include 重复登记）。
// 头文件声明的 sourceFile 置空 = 全编译单元可见（与 R7 合成声明同口径，
// C 翻译单元语义）；头文件解析进 Parser 头文件模式（R9 声明子集：函数
// 原型、限定符/修饰符链、(void) 空参表、数组形参退化）。头文件进依赖
// 清单 loadOrder（-MMD 增量追踪）。
class Loader {
public:
    // entryFiles：一个或多个入口 .nc 文件（对应命令行多个输入，逐个作为
    // 装载根合并为一个编译单元；跨根重复 import 仍幂等）
    explicit Loader(std::vector<std::string> entryFiles)
      : m_entryFiles(std::move(entryFiles)) {
        if (!m_entryFiles.empty()) {
            m_entryDir = std::filesystem::path(m_entryFiles.front())
                           .parent_path()
                           .lexically_normal();
        }
    }

    LoadResult load() {
        LoadResult result;
        result.program = std::make_unique<Program>(1, 1);

        std::vector<std::string> chain; // DFS 活动栈（显示路径，用于 import 链）
        for (const std::string& entry : m_entryFiles) {
            const std::filesystem::path path(entry);
            if (!loadUnit(path, path, 0, 0, chain, result)) {
                result.ok = false;
                result.program.reset();
                return result;
            }
        }

        result.program->declarations = std::move(m_mergedDecls);
        result.loadOrder = std::move(m_order);
        result.ok = true;
        return result;
    }

    // 独立编译装载（PRD R7 轻装载）：entryPath 单模块产出自己的编译单元，
    // import 闭包只提取导出符号签名（不合并声明、不参与语义/代码生成）。
    //
    // 与 load() 的分工（二期编译模型，PRD R7：每模块独立产出中间产物）：
    // - load()：整体编译——依赖闭包全部声明合并为单编译单元；
    // - loadStandalone()：独立编译——本模块声明 + 依赖导出签名。签名以
    //   「合成声明」注入 program（见 StandaloneResult::program 注释），语义
    //   层按既有规则检查（extern 函数签名检查 / 全局变量类型检查），代码生成
    //   层复用既有机制（未定义被调函数 → callx + 汇编头 extern 行；isImported
    //   全局 → 导入表引用）。跨模块签名不一致本层不校验（边界见下）。
    //
    // 语义边界（PRD R7 一期，记录在案）：
    // - 编译期只做签名级检查：依赖模块「未导出」符号不在签名集内，引用即
    //   编译错误（undeclared）；源码手写 extern 声明可越过本层（R3 语义），
    //   这类引用由链接/加载期兜底（nas -r 后仍为动态导入，nvm 报 unresolved
    //   import）——编译期不拦截。
    // - 签名以「名字首次出现」去重：两个依赖模块导出同名符号时只保留首个，
    //   不做签名冲突报错（与 C 的重复原型宽松语义一致）。
    // - 依赖模块全局初始化器不跨模块执行：各模块初始化代码留在各自产物内，
    //   链接产物只从导出 main 进入（见 codegen 的 default main / 全局初始化）。
    //
    // 与 load() 一样是一次性 API：同一 Loader 实例不要混用两种装载模式
    // （memo/顺序状态共享）。
    StandaloneResult loadStandalone(const std::string& entryPath) {
        StandaloneResult result;
        m_entryDir = std::filesystem::path(entryPath).parent_path().lexically_normal();
        const std::filesystem::path path(entryPath);
        m_lightEntryCanonical = canonicalOf(path);

        std::vector<std::string> chain;
        std::unique_ptr<Program> entryProgram;
        if (!lightUnit(path, path, 0, 0, chain, result, entryProgram)) {
            result.ok = false;
            return result;
        }

        // 合成依赖签名声明（依赖在前，与整体编译 DFS 先序一致）→ 拼接本模块
        auto program = std::make_unique<Program>(1, 1);
        program->imports = entryProgram->imports;
        for (const ImportedSymbol& symbol : m_collected) {
            program->declarations.push_back(synthesizeDecl(symbol));
        }
        for (auto& decl : entryProgram->declarations) {
            program->declarations.push_back(std::move(decl));
        }

        result.program = std::move(program);
        result.loadOrder = std::move(m_order);
        result.imports = std::move(m_collected);
        result.ok = true;
        return result;
    }

private:
    // 装载一个文件并按 DFS 先序拼接声明。importerSite 为触发本次装载的
    // import 语句位置（入口文件传自身路径与 0,0），错误诊断落在该处。
    bool loadUnit(const std::filesystem::path& path,
                  const std::filesystem::path& importerPath,
                  int importerLine,
                  int importerColumn,
                  std::vector<std::string>& chain,
                  LoadResult& result) {
        const std::string canonical = canonicalOf(path);
        const std::string display = displayOf(path);
        const DiagnosticLocator locator{ displayOf(importerPath),
                                         importerLine,
                                         importerColumn };

        // 环形 import：目标在 DFS 活动栈中 → 报错含完整链
        if (m_inStack.find(canonical) != m_inStack.end()) {
            std::string chainText;
            for (const std::string& node : chain) {
                chainText += node + " -> ";
            }
            chainText += display;
            result.diagnostics.push_back(
              makeError(locator, "circular import: " + chainText));
            return false;
        }
        // 重复 import 幂等：已完成装载的文件跳过（声明已在首次 import 处拼接）
        if (m_loaded.find(canonical) != m_loaded.end()) {
            return true;
        }

        // 读文件
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            result.diagnostics.push_back(
              makeError(locator, "cannot open input file '" + display + "'"));
            return false;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        const std::string source = buf.str();

        // R9 行级预处理（include C 头文件·声明子集）：#include "x.h" 引号形式
        // 递归展开（头文件段 DFS 首现序，先于本文件段产出）、guard/#pragma
        // once 识别、对象宏/enum 常量、不支持指令报错。预处理诊断与装载诊断
        // 同格式
        std::vector<PrepFile> prepFiles;
        if (!m_preprocessor
               .process(path, canonical, display, false, prepFiles, result.diagnostics)) {
            return false;
        }

        // 逐段解析：头文件段进头文件模式（R9 声明子集），其声明 sourceFile
        // 置空 = 全编译单元可见（与 R7 合成声明同口径，C 的单元级可见语义）；
        // .nc 段沿用既有规则（sourceFile = 显示路径）
        std::vector<ImportDirective> fileImports;
        std::vector<std::unique_ptr<Decl>> fileDecls;
        for (const PrepFile& prep : prepFiles) {
            std::unique_ptr<Program> piece;
            try {
                Lexer lexer(prep.text);
                const std::vector<Token> tokens = lexer.tokenize();
                Parser parser(tokens, prep.display, m_typedefNames);
                if (prep.isHeader) {
                    parser.setHeaderMode(true);
                }
                piece = parser.parse();
            } catch (const ParseError& e) {
                result.diagnostics.push_back(
                  makeError({ e.file, e.line, e.column }, e.message));
                return false;
            } catch (const std::exception& e) {
                result.diagnostics.push_back(
                  makeError({ prep.display, 1, 1 },
                            std::string("cannot parse file: ") + e.what()));
                return false;
            }
            if (prep.isHeader) {
                // 头文件进依赖清单（-MMD 增量构建追踪头文件变更）
                m_order.push_back(prep.display);
            } else {
                fileImports = std::move(piece->imports);
            }
            for (auto& decl : piece->declarations) {
                if (decl->type == ASTNodeType::TYPEDEF_DECLARATION) {
                    // 单元级 typedef 名线程（PRD R9）：后续文件/段的 Parser
                    // 据此把头文件别名按类型名解析
                    m_typedefNames.insert(
                      static_cast<const TypedefDeclaration&>(*decl).alias);
                }
                decl->sourceFile = prep.isHeader ? std::string() : display;
                fileDecls.push_back(std::move(decl));
            }
        }

        // 入栈 → 先递归装载全部 import（import 语法上位于文件顶部，
        // 递归序与文本顺序一致）→ 弹栈 → 拼接本文件声明
        chain.push_back(display);
        m_inStack.insert(canonical);
        for (const ImportDirective& directive : fileImports) {
            const auto resolved = resolveImport(directive, path);
            if (!resolved.first) {
                result.diagnostics.push_back(
                  makeError({ display, directive.line, directive.column },
                            "cannot find module '" + directive.target + "' (looked for '"
                              + canonicalOf(resolved.second) + "')"));
                return false;
            }
            if (!loadUnit(resolved.second,
                          path,
                          directive.line,
                          directive.column,
                          chain,
                          result)) {
                return false;
            }
        }
        chain.pop_back();
        m_inStack.erase(canonical);

        m_loaded.insert(canonical);
        m_order.push_back(display);
        for (auto& decl : fileDecls) {
            m_mergedDecls.push_back(std::move(decl));
        }
        return true;
    }

    // ---- 独立编译轻装载（PRD R7）----

    // 轻装载一个模块：解析后递归处理其 import（只提取签名，不合并声明），
    // 完成序记录依赖清单。入口模块自身不收集导出（入口的导出无需注入自身）。
    // entryProgram 出参携带入口模块的解析结果（含 import 指令与自身声明）。
    bool lightUnit(const std::filesystem::path& path,
                   const std::filesystem::path& importerPath,
                   int importerLine,
                   int importerColumn,
                   std::vector<std::string>& chain,
                   StandaloneResult& result,
                   std::unique_ptr<Program>& entryProgram) {
        const std::string canonical = canonicalOf(path);
        const std::string display = displayOf(path);
        const DiagnosticLocator locator{ displayOf(importerPath),
                                         importerLine,
                                         importerColumn };

        // 环形 import 与整体编译同口径报错（含完整链）
        if (m_inStack.find(canonical) != m_inStack.end()) {
            std::string chainText;
            for (const std::string& node : chain) {
                chainText += node + " -> ";
            }
            chainText += display;
            result.diagnostics.push_back(
              makeError(locator, "circular import: " + chainText));
            return false;
        }
        // 重复 import 幂等：签名已在首次 import 处收集
        if (m_lightLoaded.find(canonical) != m_lightLoaded.end()) {
            return true;
        }

        // 读文件
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            result.diagnostics.push_back(
              makeError(locator, "cannot open input file '" + display + "'"));
            return false;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        const std::string source = buf.str();

        // R9 行级预处理（与 loadUnit 同口径）：头文件段并入本模块编译单元
        std::vector<PrepFile> prepFiles;
        if (!m_preprocessor
               .process(path, canonical, display, false, prepFiles, result.diagnostics)) {
            return false;
        }

        // 逐段解析（与 loadUnit 同策略：头文件头文件模式 + sourceFile 置空）
        auto fileProgram = std::make_unique<Program>(1, 1);
        for (const PrepFile& prep : prepFiles) {
            std::unique_ptr<Program> piece;
            try {
                Lexer lexer(prep.text);
                const std::vector<Token> tokens = lexer.tokenize();
                Parser parser(tokens, prep.display, m_typedefNames);
                if (prep.isHeader) {
                    parser.setHeaderMode(true);
                }
                piece = parser.parse();
            } catch (const ParseError& e) {
                result.diagnostics.push_back(
                  makeError({ e.file, e.line, e.column }, e.message));
                return false;
            } catch (const std::exception& e) {
                result.diagnostics.push_back(
                  makeError({ prep.display, 1, 1 },
                            std::string("cannot parse file: ") + e.what()));
                return false;
            }
            if (prep.isHeader) {
                m_order.push_back(prep.display);
            } else {
                fileProgram->imports = std::move(piece->imports);
            }
            for (auto& decl : piece->declarations) {
                if (decl->type == ASTNodeType::TYPEDEF_DECLARATION) {
                    m_typedefNames.insert(
                      static_cast<const TypedefDeclaration&>(*decl).alias);
                }
                // 轻装载保持既有口径：声明 sourceFile 恒置空（独立编译的可见
                // 性 = 单文件模式，含 R9 头文件段与合成注入声明的互见规则）
                decl->sourceFile = std::string();
                fileProgram->declarations.push_back(std::move(decl));
            }
        }

        chain.push_back(display);
        m_inStack.insert(canonical);
        for (const ImportDirective& directive : fileProgram->imports) {
            const auto resolved = resolveImport(directive, path);
            if (!resolved.first) {
                result.diagnostics.push_back(
                  makeError({ display, directive.line, directive.column },
                            "cannot find module '" + directive.target + "' (looked for '"
                              + canonicalOf(resolved.second) + "')"));
                return false;
            }
            if (!lightUnit(resolved.second,
                           path,
                           directive.line,
                           directive.column,
                           chain,
                           result,
                           entryProgram)) {
                return false;
            }
        }
        chain.pop_back();
        m_inStack.erase(canonical);

        m_lightLoaded.insert(canonical);
        m_order.push_back(display);
        // 入口模块的导出不需要注入自身；依赖模块导出签名按名字去重（首现
        // 优先），收集顺序 = DFS 后序（依赖先于导入者）
        if (canonical == m_lightEntryCanonical) {
            entryProgram = std::move(fileProgram);
        } else {
            collectExports(*fileProgram, display);
        }
        return true;
    }

    // 收集一个依赖模块的顶层导出签名（export 函数/全局变量；extern 声明与
    // 私有符号、struct/typedef 不在签名范围）
    void collectExports(const Program& program, const std::string& moduleDisplay) {
        for (const auto& decl : program.declarations) {
            if (decl->type == ASTNodeType::FUNC_DECLARATION) {
                const auto& func = static_cast<const FuncDeclaration&>(*decl);
                if (!func.isExported || func.isExtern) {
                    continue;
                }
                if (!m_collectedByName.insert(func.name).second) {
                    continue; // 同名导出已收集（首现优先）
                }
                ImportedSymbol symbol;
                symbol.name = func.name;
                symbol.module = moduleDisplay;
                symbol.isFunction = true;
                symbol.line = func.line;
                symbol.column = func.column;
                symbol.returnType = func.returnType;
                symbol.returnIsStruct = func.returnIsStruct;
                symbol.returnPointerDepth = func.returnPointerDepth;
                for (const auto& param : func.parameters) {
                    ImportedSymbol::Param p;
                    p.name = param->name;
                    p.type = param->type;
                    p.isStructTag = param->isStructTag;
                    p.pointerDepth = param->pointerDepth;
                    symbol.params.push_back(std::move(p));
                }
                m_collected.push_back(std::move(symbol));
            } else if (decl->type == ASTNodeType::VAR_DECLARATION) {
                const auto& var = static_cast<const VarDeclaration&>(*decl);
                if (!var.isExported) {
                    continue;
                }
                if (!m_collectedByName.insert(var.name).second) {
                    continue;
                }
                ImportedSymbol symbol;
                symbol.name = var.name;
                symbol.module = moduleDisplay;
                symbol.isFunction = false;
                symbol.line = var.line;
                symbol.column = var.column;
                symbol.type = var.type;
                symbol.isStructTag = var.isStructTag;
                symbol.pointerDepth = var.pointerDepth;
                symbol.isArray = var.isArray;
                symbol.arraySize = var.arraySize;
                symbol.arrayDims = var.arrayDims;
                m_collected.push_back(std::move(symbol));
            }
        }
    }

    // 依赖签名 → 注入声明（sourceFile 置空 = 按单文件可见性规则全单元可见）：
    // - 函数 → isExtern 声明（PRD R3 既有路径：语义签名检查、IR externs、
    //   codegen callx + 汇编头 extern 行）
    // - 全局变量 → isImported 声明（无初始化器；codegen 据此走导入表引用）
    std::unique_ptr<Decl> synthesizeDecl(const ImportedSymbol& symbol) const {
        if (symbol.isFunction) {
            auto func = std::make_unique<FuncDeclaration>(symbol.returnType,
                                                          symbol.name,
                                                          symbol.line,
                                                          symbol.column);
            func->returnIsStruct = symbol.returnIsStruct;
            func->returnPointerDepth = symbol.returnPointerDepth;
            for (const ImportedSymbol::Param& p : symbol.params) {
                auto param = std::make_unique<VarDeclaration>(p.type,
                                                              p.name,
                                                              symbol.line,
                                                              symbol.column);
                param->isStructTag = p.isStructTag;
                param->pointerDepth = p.pointerDepth;
                func->parameters.push_back(std::move(param));
            }
            func->isExtern = true;
            func->isVariadic = symbol.isVariadic;
            return func;
        }
        auto var = std::make_unique<VarDeclaration>(symbol.type,
                                                    symbol.name,
                                                    symbol.line,
                                                    symbol.column);
        var->isStructTag = symbol.isStructTag;
        var->pointerDepth = symbol.pointerDepth;
        var->isArray = symbol.isArray;
        var->arraySize = symbol.arraySize;
        var->arrayDims = symbol.arrayDims;
        var->isImported = true;
        return var;
    }

    // import 路径解析（相对导入者目录）：first = 是否命中，second = 命中的
    // 候选路径（未命中时为最后尝试的候选，供诊断输出）
    std::pair<bool, std::filesystem::path>
    resolveImport(const ImportDirective& directive,
                  const std::filesystem::path& importer) const {
        const std::filesystem::path importerDir =
          importer.parent_path().lexically_normal();
        std::filesystem::path candidate =
          importerDir
          / (directive.quoted ? std::filesystem::path(directive.target)
                              : std::filesystem::path(directive.target + ".nc"));
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return { true, candidate };
        }
        // 带引号路径省略 .nc 后缀时补试一次（import "math"; ≈ import "math.nc";）
        if (directive.quoted && candidate.extension() != ".nc") {
            std::filesystem::path withExt = candidate;
            withExt += ".nc";
            if (std::filesystem::exists(withExt, ec)) {
                return { true, withExt };
            }
            candidate = withExt;
        }
        return { false, candidate };
    }

    struct DiagnosticLocator {
        std::string file;
        int line = 0;
        int column = 0;
    };

    static Diagnostic makeError(const DiagnosticLocator& locator,
                                const std::string& message) {
        Diagnostic diagnostic;
        diagnostic.file = locator.file;
        diagnostic.line = locator.line;
        diagnostic.column = locator.column;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.message = message;
        return diagnostic;
    }

    // 规范路径（环形检测与幂等的键）：绝对路径 + 词法规范化 + 正斜杠
    static std::string canonicalOf(const std::filesystem::path& path) {
        return std::filesystem::absolute(path).lexically_normal().generic_string();
    }

    // 显示路径：优先相对入口目录（同目录工程呈现 a.nc -> b.nc -> a.nc），
    // 无法相对化（跨盘/上级目录）时回退规范化绝对路径
    std::string displayOf(const std::filesystem::path& path) const {
        const std::filesystem::path normalized = path.lexically_normal();
        std::error_code ec;
        const auto rel = normalized.lexically_relative(m_entryDir);
        if (!ec && !rel.empty() && *rel.begin() != "..") {
            return rel.generic_string();
        }
        return normalized.generic_string();
    }

    std::vector<std::string> m_entryFiles;
    std::filesystem::path m_entryDir;
    std::set<std::string> m_loaded;   // 已完成装载的规范路径（幂等 memo）
    std::set<std::string> m_inStack;  // DFS 活动栈（环形检测）
    std::vector<std::string> m_order; // 装载完成顺序（依赖清单，含头文件）
    std::vector<std::unique_ptr<Decl>> m_mergedDecls; // 合并声明流（DFS 先序）

    // ---- R9 include C 头文件 ----
    // 单元级预处理器：常量表（对象宏/enum 常量/固定宽度预置）与已处理头
    // 文件集跨文件共享——头文件每编译单元只展开一次（重复/循环 include
    // 幂等跳过），typedef 别名线程给后续文件的 Parser
    Preprocessor m_preprocessor;
    std::set<std::string> m_typedefNames;

    // ---- 独立编译轻装载状态（PRD R7，与 load() 的 memo 共享 m_order/m_inStack）----
    std::set<std::string> m_lightLoaded;     // 轻装载完成 memo（规范路径）
    std::string m_lightEntryCanonical;       // 入口模块规范路径（导出不注入自身）
    std::vector<ImportedSymbol> m_collected; // 依赖导出签名（收集序 = DFS 后序）
    std::set<std::string> m_collectedByName; // 签名去重（首现优先）
};

#endif // NCC_LOADER_H

#ifndef NCC_LOADER_H
#define NCC_LOADER_H

#include "ncc/ast.hpp"
#include "ncc/lexer.hpp"
#include "ncc/parser.hpp"
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

// 多文件装载器（PRD R2a「文件即模块」，一期整体编译）。
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

        // 解析（解析错误 → 结构化诊断，带文件与行列）
        std::unique_ptr<Program> fileProgram;
        try {
            Lexer lexer(source);
            const std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens, display);
            fileProgram = parser.parse();
        } catch (const ParseError& e) {
            result.diagnostics.push_back(
              makeError({ e.file, e.line, e.column }, e.message));
            return false;
        } catch (const std::exception& e) {
            result.diagnostics.push_back(
              makeError(locator, std::string("cannot parse file: ") + e.what()));
            return false;
        }

        // 入栈 → 先递归装载全部 import（import 语法上位于文件顶部，
        // 递归序与文本顺序一致）→ 弹栈 → 拼接本文件声明
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
        for (auto& decl : fileProgram->declarations) {
            decl->sourceFile = display;
            m_mergedDecls.push_back(std::move(decl));
        }
        return true;
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
    std::set<std::string> m_loaded;                   // 已完成装载的规范路径（幂等 memo）
    std::set<std::string> m_inStack;                  // DFS 活动栈（环形检测）
    std::vector<std::string> m_order;                 // 装载完成顺序（依赖清单）
    std::vector<std::unique_ptr<Decl>> m_mergedDecls; // 合并声明流（DFS 先序）
};

#endif // NCC_LOADER_H

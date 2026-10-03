-- NanoC 独立编译驱动 ncc-separate 的构建定义（PRD R7/R8）。
--
-- 用法（消费方工程）：
--     includes("<nanoc SDK>/rules/nanoc/driver/xmake.lua")  -- 引入 target 定义
--     target("app")
--         set_kind("binary")
--         set_toolchains("nanoc")                 -- 见 rules/nanoc/toolchain.lua
--         add_deps("ncc-separate", {order = true}) -- 只保证构建顺序，不继承链接
--         add_files("src/**.nc")
--
-- {order = true}：ncc-separate 是"构建本工程要用的工具"，不是链接输入——
-- order 依赖只排构建顺序、不参与链接（普通 add_deps 会把依赖目标的链接
-- 配置继承进 app，语义错误）。
--
-- 也可以不 include 本文件：自行从 SDK 构建 ncc-separate 后放 PATH / 用
-- 环境变量 NANOC 指定，toolchain 会按序探测（见 toolchain.lua 注释）。
--
-- 说明：
-- - 只编 ncc 前端公开组件（lexer/parser/ast/semantic/ir/c_backend + loader
--   为纯头），不需要 spdlog（仅 ncc 主 CLI 用），llvm_backend/codegen（汇编
--   后端）也不在依赖闭包内。
-- - libca 锁与主工程相同的版本与模块集，避免同仓多版本包并存。
-- - set_default(false)：普通 `xmake` 不构建它；被 app 的依赖引用时按需构建。
-- - build.fence：本 target 是消费方的"编译工具"——依赖本 target 的目标必须
--   等 ncc-separate.exe 构建完成才能执行其 .nc 编译命令（fence 挂在被依赖
--   方，见 modules/private/action/build/target.lua 的 target_fence）。

add_repositories("luiox-repo https://github.com/luiox/luiox-repo.git")
add_requires("libca 0.0.8", {configs = {modules = "core,str,collection,fs,opt,log"}})

target("ncc-separate")
    set_kind("binary")
    set_default(false)
    set_policy("build.fence", true)
    add_includedirs(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src"))
    add_files(path.join(os.scriptdir(), "ncc_separate.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "lexer.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "parser.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "ast.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "semantic.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "ir.cpp"))
    add_files(path.join(os.scriptdir(), "..", "..", "..", "ncc", "src", "ncc", "c_backend.cpp"))
    add_packages("libca")

-- vim: ts=4 sw=4 et

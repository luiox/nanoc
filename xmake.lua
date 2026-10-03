add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

set_languages("c99")
set_languages("c++17")

-- MSVC 源码/执行字符集统一为 UTF-8：源文件多为无 BOM 的 UTF-8（含中文注释与
-- 字符串字面量），缺此标志时 cl 按系统 CP936 误读，奇数字节后跟引号会吞掉
-- 闭引号引发级联语法错误（gcc/clang 无此参数，限定 cl）
add_cxflags("/utf-8", {tools = "cl"})

-- 添加包依赖
add_requires("gtest", "spdlog")

-- libca 基础库（PRD R14：编译器新代码一律使用 libca；锁小版本，按需裁剪模块）
add_repositories("luiox-repo https://github.com/luiox/luiox-repo.git")
add_requires("libca 0.0.8", {configs = {modules = "core,str,collection,fs,opt,log"}})

-- NanoComplier
target("ncc")
    set_kind("binary")
    add_includedirs("ncc/src")
    add_files("ncc/src/ncc/**.cpp")
    add_packages("spdlog", "libca")

-- NanoVM
target("nvm")
    set_kind("binary")
    add_includedirs("nvm/src")
    add_files("nvm/src/nvm/**.cpp")

-- NanoAssembler
target("nas")
    set_kind("binary")
    add_includedirs("nas/src")
    add_includedirs("nvm/src")
    add_files("nas/src/nas/main.cpp")
    add_files("nas/src/nas/instruction.cpp")
    add_files("nas/src/nas/linker.cpp")

-- 测试目标
target("tests")
    set_kind("binary")
    add_includedirs("nvm/src")
    add_includedirs("nas/src")
    add_includedirs("ncc/src")
    add_files("tests/**.cpp")
    add_files("nvm/src/nvm/core.cpp")
    add_files("nvm/src/nvm/handlers/**.cpp")
    add_files("nas/src/nas/instruction.cpp")
    add_files("nas/src/nas/linker.cpp")
    add_files("ncc/src/ncc/lexer.cpp")
    add_files("ncc/src/ncc/parser.cpp")
    add_files("ncc/src/ncc/codegen.cpp")
    add_files("ncc/src/ncc/ast.cpp")
    add_files("ncc/src/ncc/semantic.cpp")
    add_files("ncc/src/ncc/ir.cpp")
    add_packages("gtest", "spdlog", "libca")

-- 编译示例程序
target("compile_examples")
    set_kind("binary")
    add_includedirs("ncc/src")
    add_files("examples/compile_examples.cpp")
    add_files("ncc/src/ncc/lexer.cpp")
    add_files("ncc/src/ncc/parser.cpp")
    add_files("ncc/src/ncc/codegen.cpp")
    add_files("ncc/src/ncc/ast.cpp")
    add_packages("spdlog")

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode


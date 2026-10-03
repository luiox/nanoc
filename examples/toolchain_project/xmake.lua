-- NanoC 样例工程：toolchain("nanoc") 正式形态（PRD R8 / M4）
--
-- 与 hello_project（rule 形态）的差异：
--   rule 形态   = 整体编译（全部 .nc 一次交给 ncc --emit=c，产出单个 .c）
--   toolchain 形态 = 按文件独立编译（每 .nc → 各自 .c → 各自 .obj）+ 目标级
--   C 链接；改单个 .nc 只重编该文件（文件级增量）。
--
-- 用法（在 SDK 仓库内需 -P .，避免向上命中 SDK 根工程；详见 README.md）：
--   cd examples/toolchain_project
--   xmake -P . -y         # 构建：先建 ncc-separate 驱动，再按文件编 .nc
--   xmake run -P . app    # 运行，退出码 49
--
-- 工程形态（set_toolchains("nanoc") + add_rules + add_files）：
--   - add_rules("nanoc.toolchain")：.nc 的按文件构建规则（xmake 的 language
--     注册表只在安装目录，项目级无法注册扩展名，需显式挂规则——与 go/dlang
--     内置语言的差异，详见 doc/xmake-toolchain.md）
--   - set_toolchains("nanoc")：正式形态标记；漏写时规则会自动补挂
--   - add_deps("ncc-separate")：现场构建独立编译驱动（不写则按 NANOC/PATH 探测）

add_rules("mode.debug", "mode.release")

-- 引入 NanoC SDK 提供的 toolchain("nanoc") 与独立编译驱动定义。
-- 本样例按相对路径直接 include SDK 仓库里的文件；真实工程把 NanoC SDK
-- clone/安装到任意位置后 include 同款路径即可（不依赖 SDK 的构建系统）。
includes("../../rules/nanoc/toolchain.lua")
includes("../../rules/nanoc/driver/xmake.lua")

target("app")
    set_kind("binary")
    set_toolchains("nanoc")
    add_rules("nanoc.toolchain")
    add_deps("ncc-separate")
    add_files("src/**.nc")

-- vim: ts=4 sw=4 et

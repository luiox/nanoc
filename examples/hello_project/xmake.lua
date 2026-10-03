-- NanoC 样例工程：一条 `xmake` 命令完成 .nc → C → exe（PRD R5 / M2）
--
-- 用法：
--   cd examples/hello_project
--   xmake -y              # 构建（需能找到 ncc，见 README.md）
--   xmake run hello       # 运行，退出码 42
--
-- 构建前 ncc 需可用（按优先级）：
--   1. 环境变量 NANOC 指向 ncc 可执行文件；
--   2. ncc 在 PATH 上；
--   3. 都没有 → 规则报错并给出指引。

add_rules("mode.debug", "mode.release")

-- 引入 NanoC SDK 提供的 rule("nanoc")。
-- 本样例按相对路径直接 include SDK 仓库里的规则文件；真实工程把 NanoC
-- SDK clone/安装到任意位置后 include 同款路径即可（不依赖 SDK 的构建系统）。
includes("../../rules/nanoc/nanoc.lua")

target("hello")
    set_kind("binary")
    add_rules("nanoc")
    add_files("src/**.nc")

-- vim: ts=4 sw=4 et

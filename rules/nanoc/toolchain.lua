--!A xmake toolchain for the NanoC SDK (PRD R8 / M4)
--
-- toolchain("nanoc")：把 NanoC（.nc）源码接入 xmake 构建的正式形态。
--
-- 编译模型（PRD A7 二期，R7 独立编译语义）：
--     每个 .nc 独立调用 `ncc-separate <file.nc> --emit=c -o <file>.c`
--     → 每文件一份 .c（依赖模块只注入 extern 原型，函数体仅本模块）
--     → 复用当前平台内置 C 工具链把每份 .c 编译为各自 .obj
--     → 目标级用 C 链接器链接。跨模块符号（import/export）经 C 链接器
--     按符号名天然解析；改单个 .nc 只重编该文件（文件级增量）。
--
-- 用户工程形态：
--
--     add_rules("mode.debug", "mode.release")
--     includes("path/to/nanoc/rules/nanoc/toolchain.lua") -- 引入本文件
--
--     target("app")
--         set_kind("binary")
--         set_toolchains("nanoc")
--         add_rules("nanoc.toolchain")   -- .nc 扩展名的构建规则（见下"为何
--                                        -- 还要 add_rules"）
--         add_deps("ncc-separate")       -- 可选：现场构建独立编译驱动（推荐，
--                                        -- 见 rules/nanoc/driver/xmake.lua）
--         add_files("src/**.nc")
--
-- 为何还要 add_rules("nanoc.toolchain")（xmake 机制边界，如实记录）：
--     go/dlang 这类"只 set_toolchains + add_files"的语言，依赖 xmake 内置
--     language 注册表（extension → sourcekind → 自动挂规则）。该注册表
--     只从 xmake 安装目录的 languages/ 加载（core/language/language.lua
--     的 _directory()），项目级无法注册自定义 language。因此 .nc 的分类
--     走 rule 的 set_extensions + 显式 add_rules（与 xmake 自带
--     cppfront/protobuf 规则同款）。rule 的 on_load 会自动补挂
--     toolchain("nanoc")（仿 dlang），所以漏写 set_toolchains 也能构建。
--
-- ncc-separate 驱动（为何不是 ncc 本体）：
--     R7 的独立编译轻装载 Loader::loadStandalone（#54）只交付了 API，ncc
--     主 CLI 的 `ncc <file> --emit=c` 仍走整体装载——import 闭包的定义会
--     被并入每份产物，两模块各自产 .c 后 C 链接报重复定义（LNK2005/
--     LNK1169）。#54 注明 CLI 旗标接线"下一轮统一做"；本形态按文件编译
--     使用 SDK 提供的独立驱动 ncc-separate（rules/nanoc/driver/，复用 ncc
--     公开组件，不改 ncc/**）。未来 ncc 主 CLI 加 --separate 后可切换。
--
-- ncc-separate 获取策略（按优先级；探测逻辑复制自 rule("nanoc") 的 ncc
-- 探测并按驱动名调整——两文件的钩子在重载沙箱里执行，无法共享顶层函数，
-- 复制换自包含，见 nanoc.lua 同款注释）：
--     1. 环境变量 NANOC：指向含 ncc-separate 的目录，或直接指向
--        ncc-separate 可执行文件（无效——如指向 ncc 主程序——只警告并
--        继续探测，与 rule 形态的直接报错不同：同一工程常把 NANOC 设给
--        rule 形态，两形态并存时不应互相卡死）；
--     2. 本工程内名为 ncc-separate 的 target（典型：include
--        rules/nanoc/driver/xmake.lua 现场构建；配合 add_deps +
--        build.fence 保证先建驱动后编 .nc）；
--     3. PATH 中查找 ncc-separate（lib.detect.find_tool）；
--     4. 都找不到 → 报错并给出安装/指定指引。
--
-- 实现机制说明：
--     形态参照 xmake 自带 cppfront 规则（"每文件代码生成 → 交给 C/C++
--     规则编译"的按文件范式）：on_buildcmd_file（单数）把每个 .nc 的
--     "ncc-separate 产 .c + C 工具链编 .obj"注册为 jobgraph 里独立节点，
--     各带 depend 缓存——改哪个 .nc 就只重跑哪个节点，链接在目标级照常
--     收集 objectfiles。这是与 rule("nanoc")（on_buildcmd_files 复数，
--     整体一个节点）的本质差异，也是 R8"按文件增量"验收的机制基础。
--
--     只含 .nc 的目标没有任何 C 源，sourcekinds 为空会导致链接器选型
--     缺失——on_load 仿 cppfront 补一个 "cc" sourcekind（只影响链接器
--     选型，不产生编译动作）。
--
--     toolchain("nanoc") 本身是"部分工具链"（仿 xmake 自带 nasm）：不
--     定义 cc/ld——中间 .c 的编译与最终链接复用当前平台默认 C 工具链
--     （xmake 对部分工具链自动追加平台 standalone 工具链，见
--     core/project/target.lua target:toolchains() 注释）。故不设
--     set_kind("standalone")，也无需在 toolset 里重复探测（探测权威在
--     rule 钩子，构建期进行，报错带完整指引）。
--
-- 已知边界（如实记录）：
--     - 跨模块签名漂移不触发导入方重编：每文件的增量键 = 该 .nc 自身，
--       依赖模块只以 extern 原型进入产物（链接按符号名解析）。改 math.nc
--       的函数签名不会自动重编 import 它的 main.nc——需 clean 或手动
--       touch（与 R7 签名级检查边界一致）。需要闭包级增量的调用方可自行
--       用 ncc-separate 的 -MMD 依赖文件（含 import 闭包）。
--     - 手写 extern 声明（PRD R3）调用宿主/C 函数：toolchain 形态下走
--       C 原生链接——生成的 .c 里是 extern 原型，链接时从系统库/用户
--       add_links 解析，无需宿主库。这与 VM 后端形态（nvm 宿主库 +
--       CALLX 动态导入）不同，详见 doc/xmake-toolchain.md。
--     - 若目标自定义了 on_build，规则脚本会被 xmake 跳过（内置规则的
--       共性）。
--     - 需要 ncc-separate（本仓库 rules/nanoc/driver/ 可构建）。旧版 SDK
--       没有该驱动，按报错指引构建即可。

-- define rule: nanoc.toolchain.build（内部规则：.nc 按文件编译）
rule("nanoc.toolchain.build")
    set_extensions(".nc")

    -- 每个 .nc 一个独立 job（on_buildcmd_file 单数，cppfront 同款）：
    -- ncc-separate 产 .c → C 工具链编 .obj。增量缓存：该 .nc 变化才重跑。
    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)

        -- import 须写在函数内：经 includes() 装载的规则文件里，顶层
        -- import 不可见（钩子重载沙箱只有函数内 import 可用）
        import("lib.detect.find_tool")
        import("core.project.project")

        -- ---------------------------------------------------------------
        -- 探测 ncc-separate：NANOC 环境变量 → 工程内 target → PATH 查找
        -- → 报错给指引。结果缓存在 target data（同一目标只探测一次）。
        -- 探测发生在构建期（jobgraph 准备阶段），xmake config/run 等动作
        -- 不受缺驱动影响。
        -- ---------------------------------------------------------------
        local nccsep = target:data("nanoc.toolchain.nccsep")
        if not nccsep then
            -- 1) 环境变量 NANOC：目录 → 补 ncc-separate(.exe)；文件 → 须是
            --    ncc-separate 本体。与 rule("nanoc") 的差异：这里 NANOC 无效
            --    只警告并继续（rule 形态是直接报错）——同一工程常把 NANOC
            --    设给 rule 形态的 ncc 主程序，toolchain 形态应继续探测工程
            --    内 target / PATH，全落空时才报错并提示 NANOC 已设但无驱动
            local env_ncc = os.getenv("NANOC")
            if env_ncc and #env_ncc > 0 then
                local candidate = env_ncc
                if os.isdir(candidate) then
                    local exe = (os.host() == "windows") and ".exe" or ""
                    candidate = path.join(candidate, "ncc-separate" .. exe)
                end
                if os.isfile(candidate) then
                    nccsep = candidate
                else
                    wprint("toolchain(nanoc): 环境变量 NANOC=%s 下没有 ncc-separate" ..
                        "（ncc 主程序只支持整体编译），继续按工程内 target / PATH 探测",
                        env_ncc)
                end
            end
            -- 2) 工程内名为 ncc-separate 的 target（现场构建驱动）。此处只取
            --    路径不查存在性：jobgraph 准备阶段驱动可能还没构建完（首建），
            --    add_deps + build.fence 保证执行到本命令时驱动已就绪
            if not nccsep then
                local deptarget = project.target("ncc-separate")
                if deptarget then
                    nccsep = deptarget:targetfile()
                end
            end
            -- 3) PATH 查找（norun = 只定位不试运行；force = 绕过 find_tool
            --    的持久检测缓存——负结果也会被缓存，导致"先无驱动构建失败、
            --    装好再构建仍报找不到"，除非手动清缓存）
            if not nccsep then
                local envs = os.joinenvs(target:pkgenvs(), os.getenvs())
                local tool = find_tool("ncc-separate", {norun = true, force = true, envs = envs})
                if tool and tool.program then
                    nccsep = tool.program
                end
            end
            -- 4) 找不到 → 清晰报错 + 指引（提示 NANOC 已设但无驱动的情况）
            if not nccsep then
                raise([[
toolchain(nanoc): 未找到 NanoC 独立编译驱动 ncc-separate！
toolchain("nanoc") 按文件编译需要 ncc-separate（PRD R7 独立编译语义的 CLI 接线，
ncc 主 CLI 暂无 --separate 旗标，见 rules/nanoc/driver/ncc_separate.cpp）。
解决办法（任选其一）：
  1. 工程内现场构建（推荐）：include 驱动定义并加为依赖：
       includes("<nanoc SDK>/rules/nanoc/driver/xmake.lua")
       target("app")
           ...
           add_deps("ncc-separate")
  2. 自行从 SDK 构建后告诉 xmake（任选）：
     - 设置环境变量 NANOC 指向含 ncc-separate 的目录（SDK 构建输出目录即可）：
         Windows:   set NANOC=<nanoc 仓库>\build\windows\x64\release
         Linux/mac: export NANOC=<nanoc 仓库>/build/linux/x86_64/release
     - 或把 ncc-separate 放入 PATH
详见 doc/xmake-toolchain.md。]])
            end
            target:data_set("nanoc.toolchain.nccsep", nccsep)
        end

        -- 生成的 .c：镜像源码相对路径铺在 target 的 autogen 目录下，
        -- 同名不同目录的 .nc 天然不冲突；objectfile 由 xmake 按路径唯一化
        local gencfile = target:autogenfile((sourcefile:gsub("%.nc$", ".c")),
            {rules = "nanoc.toolchain.build"})
        local objectfile = target:objectfile(gencfile)

        -- 链接阶段收集 objectfile（构建期注入即可，cppfront 同款手法）
        table.insert(target:objectfiles(), objectfile)

        -- 每文件两步：ncc-separate 独立编译产 .c（依赖模块 = extern 原型）
        -- → 复用内置 C 规则把 .c 编译为 .obj（按 .c 扩展名分派 cc，
        -- 自动套用平台默认 C 工具链与编译配置）
        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.nanoc %s", sourcefile)
        batchcmds:mkdir(path.directory(gencfile))
        batchcmds:vrunv(nccsep, {sourcefile, "--emit=c", "-o", gencfile})
        batchcmds:compile(gencfile, objectfile)

        -- 增量缓存：增量键 = 该 .nc 自身（跨模块签名漂移边界见文件头注释）；
        -- 缓存键 = objectfile 的 dependfile；lastmtime = 生成物时间
        -- （与 cppfront / rule("nanoc") 一致）
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(math.max(os.mtime(objectfile), os.mtime(gencfile)))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)

-- define rule: nanoc.toolchain（用户入口规则）
rule("nanoc.toolchain")
    add_deps("nanoc.toolchain.build")

    -- 继承依赖目标的 links/linkdirs（对齐 cppfront/dlang 的标配组合）
    add_deps("utils.inherit.links")

    -- 支持 add_files("*.o")/("*.a") 直接并入目标
    add_deps("utils.merge.object", "utils.merge.archive")

    on_load(function(target)
        -- 只有 .nc 源时目标没有任何 C sourcekind，链接器选型会缺失——
        -- 仿 cppfront 补 "cc"（只影响链接器选型，不产生编译动作）
        local sourcekinds = target:sourcekinds()
        if #sourcekinds == 0 then
            table.insert(sourcekinds, "cc")
        end

        -- 自动补挂 toolchain("nanoc")（仿 dlang）：用户只写
        -- add_rules("nanoc.toolchain") 也能构建；显式 set_toolchains("nanoc")
        -- 时不重复
        local toolchains = target:get("toolchains") or get_config("toolchain")
        if not toolchains or not table.contains(table.wrap(toolchains), "nanoc") then
            target:add("toolchains", "nanoc")
        end
    end)

-- define toolchain: nanoc（部分工具链，仿 xmake 自带 nasm）
toolchain("nanoc")
    set_homepage("https://github.com/luiox/nanoc")
    set_description("NanoC Compiler Toolchain (ncc-separate 按文件产 .c，C 链接器目标级链接)")
    -- 有意不定义 cc/ld/cxx/ar：中间 .c 的编译与最终链接复用当前平台默认
    -- C 工具链（xmake 对部分工具链自动追加平台 standalone 工具链，MSVC/
    -- gcc 环境探测与链接环境全部照旧）。也有意不定义 "nc" toolset：
    -- ncc-separate 的探测与报错口径统一收在 rule("nanoc.toolchain.build")
    -- 钩子里（构建期、带完整指引），避免两处探测口径漂移。

-- vim: ts=4 sw=4 et

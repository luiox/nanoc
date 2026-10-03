--!A xmake rule for the NanoC SDK (PRD R5 / M2)
--
-- rule("nanoc")：把 NanoC（.nc）源码接入 xmake 构建的一期形态。
--
-- 编译模型（PRD A7，一期整体编译）：
--     目标级一次调用 `ncc <全部 .nc> --emit=c -o <genc>.c`
--     → 产出一个自洽 C 翻译单元 → 复用当前平台的内置 C 工具链编译链接。
--     ncc 在此充当"前端预处理器"，语义/诊断全部由 ncc 负责。
--
-- 用户工程形态：
--
--     add_rules("mode.debug", "mode.release")
--     includes("path/to/nanoc/rules/nanoc/nanoc.lua")  -- 引入本规则文件
--
--     target("app")
--         set_kind("binary")
--         add_rules("nanoc")
--         add_files("src/**.nc")
--
-- ncc 获取策略（按优先级）：
--     1. 环境变量 NANOC：指向 ncc 可执行文件（或其所在目录）；
--     2. 在 PATH 中查找 `ncc`（lib.detect.find_tool）；
--     3. 都找不到 → 报错并给出安装/指定指引。
--
-- 实现机制说明（为何选 on_buildcmd_files 而非 before_build 钩子）：
--     参考对象是 xmake 自带的 protobuf.cpp 规则（"代码生成 → 交给 C/C++
--     规则编译"的标准范式）。on_buildcmd_files 把生成命令注册进 xmake 的
--     jobgraph：天然带 depend 缓存（仅当 .nc 输入变化才重跑 ncc）、支持
--     dry-run/项目生成器、进度显示与并行调度；而 before_build 里临时
--     add_files 的时机晚于源文件分类，且每次 xmake 调用都会触发，增量
--     语义要全部手写。生成的 .c 从不进入 target 源文件列表——编译它所需的
--     objectfile 在 after_load 阶段预先注入 target:objectfiles()（与
--     protobuf.cpp 同款，见 xmake issue #5426），链接阶段自动收集。
--
-- 结构说明：两个钩子闭包必须各自自包含（不得引用本文件顶层函数）——
--     xmake 会把规则作用域序列化进 .xmake 缓存，钩子在重载后的新沙箱里
--     执行，顶层函数与顶层 import 均不可见。需要共享的实现只有"算生成
--     .c 的路径"两行，直接在各闭包内重复，换取消耗最小。
--
-- 已知边界（一期）：
--     - 依赖文件（-MMD）接入留给 R7/R8，本期 .nc 的增量粒度 = 整个目标
--       重跑 ncc（整体编译模型本身如此，开销可忽略）；
--     - 若目标自定义了 on_build，规则脚本会被 xmake 跳过（内置规则的
--       共性，见 xmake build action 的 script 优先级）；
--     - 需要 ncc 支持 `--emit=c`（PRD R3/R4 落地后的 ncc）。旧版 ncc 会
--       自行报 "--emit=c is not supported yet"，按其提示升级即可。

-- define rule: nanoc
rule("nanoc")
    set_extensions(".nc")

    -- 目标加载后为生成的 .c 预订 objectfile 槽位。此刻 .c 还不存在也没
    -- 关系——objectfile 只是一个路径，链接阶段按 target:objectfiles()
    -- 收集；真正产出该 objectfile 的命令由下方 on_buildcmd_files 注册
    -- （protobuf.cpp 规则的同款手法）
    after_load(function(target)
        local sourcebatch = target:sourcebatches()["nanoc"]
        if not sourcebatch or #sourcebatch.sourcefiles == 0 then
            return
        end
        local gencfile = path.join(target:autogendir(), "rules", "nanoc", target:name() .. ".c")
        table.insert(target:objectfiles(), target:objectfile(gencfile))
    end)

    -- 整个 .nc 源批次一个 job：整体调用一次 ncc 产出单个 .c，再把它交给
    -- 当前 C 工具链编译为 objectfile（批命令内有序执行，无并行竞争）。
    -- 依赖缓存：任一 .nc 变化 → 重跑 ncc + 重编 .c → 重新链接。
    on_buildcmd_files(function(target, batchcmds, sourcebatch, opt)

        -- import 须写在函数内：经 includes() 装载的规则文件里，顶层
        -- import 不可见（钩子重载沙箱只有函数内 import 可用）
        import("lib.detect.find_tool")

        local sourcefiles = sourcebatch.sourcefiles
        if #sourcefiles == 0 then
            return
        end

        -- ---------------------------------------------------------------
        -- 探测 ncc：环境变量 NANOC → PATH 查找 → 报错给指引。
        -- 结果缓存在 target data（同一目标只探测一次）。构建期才探测，
        -- xmake config/run 等动作不受缺 ncc 影响。
        -- ---------------------------------------------------------------
        local ncc = target:data("nanoc.ncc")
        if not ncc then
            -- 1) 环境变量 NANOC：显式指定优先，值无效时直接报错（不静默
            --    回退 PATH，避免用户以为在用 NANOC 实际用错编译器）
            local env_ncc = os.getenv("NANOC")
            if env_ncc and #env_ncc > 0 then
                local candidate = env_ncc
                if os.isdir(candidate) then
                    local exe = (os.host() == "windows") and ".exe" or ""
                    candidate = path.join(candidate, "ncc" .. exe)
                end
                if os.isfile(candidate) then
                    ncc = candidate
                else
                    raise("rule(nanoc): 环境变量 NANOC=%s 指向的 ncc 不存在（应为可执行文件路径或其所在目录）",
                        env_ncc)
                end
            end
            -- 2) PATH 查找（norun = 只定位不试运行；force = 绕过 find_program
            --    的持久检测缓存——负结果也会被缓存，否则"先无 ncc 构建失败、
            --    装好 ncc 再构建仍报找不到"，除非手动清缓存）
            if not ncc then
                local envs = os.joinenvs(target:pkgenvs(), os.getenvs())
                local tool = find_tool("ncc", {norun = true, force = true, envs = envs})
                if tool and tool.program then
                    ncc = tool.program
                end
            end
            -- 3) 找不到 → 清晰报错 + 指引
            if not ncc then
                raise([[
rule(nanoc): 未找到 NanoC 编译器 ncc！
rule("nanoc") 需要 `ncc --emit=c` 把 .nc 源码翻译为 C，再交给系统 C 工具链编译链接。
解决办法（任选其一）：
  1. 在 NanoC SDK 仓库构建 ncc：
       cd <nanoc 仓库> && xmake build ncc
  2. 告诉 xmake ncc 的位置（任选）：
     - 设置环境变量 NANOC 指向 ncc 可执行文件：
         Windows:   set NANOC=<nanoc 仓库>\build\windows\x64\release\ncc.exe
         Linux/mac: export NANOC=<nanoc 仓库>/build/linux/x86_64/release/ncc
     - 或把 ncc（ncc.exe）放入 PATH
详见 doc/xmake-rule.md。]])
            end
            target:data_set("nanoc.ncc", ncc)
        end

        -- 生成的 .c：每目标一份，位于 target 的 autogen 目录
        local gencfile = path.join(target:autogendir(), "rules", "nanoc", target:name() .. ".c")
        local objectfile = target:objectfile(gencfile)

        -- 整体编译：ncc <files...> --emit=c -o <genc>
        local argv = table.copy(sourcefiles)
        table.join2(argv, {"--emit=c", "-o", gencfile})

        -- 进度显示：单文件显示文件名，多文件显示计数
        local display = sourcefiles[1]
        if #sourcefiles > 1 then
            display = format("%s (+%d more)", display, #sourcefiles - 1)
        end

        -- 产出中间 .c
        batchcmds:mkdir(path.directory(gencfile))
        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.nanoc %s", display)
        batchcmds:vrunv(ncc, argv)

        -- 复用内置 C 规则把生成的 .c 编译为 objectfile（按扩展名 .c 分派
        -- cc sourcekind，自动套用目标的 C 工具链与编译配置）
        batchcmds:compile(gencfile, objectfile)

        -- 增量缓存：输入 = 全部 .nc；缓存键 = objectfile 的 dependfile；
        -- lastmtime = 生成物时间（与 protobuf.cpp 一致）
        batchcmds:add_depfiles(sourcefiles)
        batchcmds:set_depcache(target:dependfile(objectfile))
        batchcmds:set_depmtime(math.max(os.mtime(objectfile), os.mtime(gencfile)))
    end)
rule_end()

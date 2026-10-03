# xmake `rule("nanoc")` 接入指南（PRD R5 / M2）

SDK 在 [`rules/nanoc/nanoc.lua`](../rules/nanoc/nanoc.lua) 提供 xmake 规则
`rule("nanoc")`：把 NanoC（`.nc`）源码接入 xmake 构建，**一条 `xmake` 命令
完成 `.nc → C → exe`**。

- 规则文件：`rules/nanoc/nanoc.lua`
- 可运行样例：[`examples/hello_project/`](../examples/hello_project/README.md)

## 快速开始

```lua
-- xmake.lua
add_rules("mode.debug", "mode.release")
includes("<nanoc SDK>/rules/nanoc/nanoc.lua")   -- 引入规则文件，路径自定

target("app")
    set_kind("binary")
    add_rules("nanoc")
    add_files("src/**.nc")
```

```console
$ xmake -y
```

可运行的最小样例见 `examples/hello_project/`（`src/main.nc` 纯计算，
退出码 42）。

## 工作机制（一期编译模型，PRD A7）

```
src/*.nc ──ncc --emit=c（目标级一次，整体编译）──► build/.gens/<t>/rules/nanoc/<t>.c
        ──内置 C 工具链编译链接（msvc/gcc/clang）──► exe
```

- ncc 充当"前端预处理器"：语义分析、诊断全部由 ncc 负责；生成的 C 是
  自洽翻译单元（含 `#include <stdint.h>/<stddef.h>`），之后就是普通 C 工程。
- 规则把 `ncc` 调用与生成的 `.c` 编译注册进 xmake 的 jobgraph
  （`on_buildcmd_files`，与 xmake 自带 protobuf.cpp 规则同款范式）：
  - **增量**：任一 `.nc` 变化 → 重跑 ncc → 重编生成 `.c` → 重新链接；
    无变化时整个链路 no-op。
  - 生成的 `.c` 不进入目标源文件列表；其 objectfile 在 `after_load` 预注入
    `target:objectfiles()`，链接阶段自动收集。
- 多文件（模块化）工程：目标内全部 `.nc` 一次性交给 ncc 整体编译（文件即
  模块，跨文件可见性按 NanoC 语义 `export`/`import` 控制，见 PRD R2a）。

## ncc 探测策略

规则在**构建期**（`xmake`/`xmake build`）按优先级探测 ncc：

| 优先级 | 来源 | 说明 |
|---|---|---|
| 1 | 环境变量 `NANOC` | 指向 ncc 可执行文件，或其所在目录（自动补 `ncc.exe`） |
| 2 | `PATH` 查找 | `lib.detect.find_tool`，绕过持久检测缓存（负缓存会过期失效） |
| 3 | 都找不到 | 构建报错，打印完整指引（构建 SDK 里的 ncc → `NANOC=` 或 PATH） |

- `NANOC` 显式指定但路径无效时**直接报错**，不静默回退 PATH——避免"以为
  在用 NANOC，实际用错编译器"。
- 探测发生在构建期：`xmake f`（config）、`xmake run` 等动作不要求 ncc 存在。

### 让 xmake 找到 ncc

```console
# 1. 在 NanoC SDK 仓库构建 ncc
$ cd <nanoc 仓库> && xmake build ncc

# 2. 任选其一告诉 xmake
$ set NANOC=<nanoc 仓库>\build\windows\x64\release\ncc.exe   # Windows cmd
$ export NANOC=<nanoc 仓库>/build/linux/x86_64/release/ncc   # Linux/mac
$ export PATH=<nanoc 仓库>/build/windows/x64/release:$PATH   # 或挂 PATH
```

## 工程目录说明（重要）

xmake 从当前目录**向上**查找工程文件时总是取**最顶层**的 `xmake.lua`。
因此：

- 用户工程在 SDK 仓库**之外**（真实场景）：直接 `xmake`；
- 在 SDK 仓库**之内**构建 `examples/hello_project`：需 `xmake -P .`，
  否则会构建到 SDK 根工程。

## 已知边界（一期）

1. **需要 `ncc --emit=c`**（PRD R3/R4 落地后的 ncc）。旧版 ncc 会自行报
   `--emit=c is not supported yet`，按提示升级即可。
2. **extern 调用 C 库函数**（`puts`/`printf` 等）依赖 R3 extern 声明；
   未合入前，样例类纯计算程序以退出码承载结果。
3. **依赖文件（-MMD）** 接入留给 R7/R8；本期 `.nc` 的增量粒度 = 整个目标
   重跑 ncc（整体编译模型本身如此，ncc 开销可忽略）。
4. **C 编译选项**：生成的 `.c` 走目标的内置 C 规则，`add_cflags`/
   `add_includedirs`/`add_links` 等照常生效于生成代码的编译。
5. **与未来 `toolchain("nanoc")`（R8）的关系**：rule 面向一期整体编译
   （`set_kind("binary")` + 复用 C 工具链）；R8 后 `set_toolchains("nanoc")`
   面向正式形态（按文件产 obj、VM 目标链接）。二者并存，选择指南届时在
   `doc/xmake-rule.md` 更新。
6. **CI**：规则不进主工程 CI 路径——根 `xmake.lua` 未 glob
   `examples/hello_project/**`，主工程构建/测试不受影响。

## 验证记录（本机，Windows x64 + MSVC 2022）

```console
$ cd examples/hello_project && xmake -P . -y
[ 23%]: <hello> compiling.nanoc src\main.nc
[ 47%]: <hello> linking.release hello.exe
[100%]: build ok, spent 0.734s
$ ./build/windows/x64/release/hello.exe; echo $?
42
```

干净机器等价验证：把 `rules/` + `examples/hello_project/` 拷至仓库外目录
（无 ncc 环境）→ 构建报"未找到 NanoC 编译器 ncc"及完整指引；设 `NANOC`
后重试 → 出 exe，退出码 42。多文件整体编译（`export` 模块）、`NANOC`
指向目录、PATH 查找三条路径均验证通过。

# xmake `toolchain("nanoc")` 接入指南（PRD R8 / M4）

SDK 在 [`rules/nanoc/toolchain.lua`](../rules/nanoc/toolchain.lua) 提供
xmake 工具链 `toolchain("nanoc")`：NanoC 作为一等编译器接入 xmake 的
**正式形态**——每个 `.nc` 独立编译、目标级链接、文件级增量。

- 模块文件：`rules/nanoc/toolchain.lua`（toolchain + 规则，一次 include 全注册）
- 独立编译驱动：[`rules/nanoc/driver/`](../rules/nanoc/driver/)（`ncc-separate`）
- 可运行样例：[`examples/toolchain_project/`](../examples/toolchain_project/README.md)
- 一期形态（整体编译 rule）：见 [`doc/xmake-rule.md`](xmake-rule.md) 与
  `rules/nanoc/nanoc.lua`

## 快速开始

```lua
-- xmake.lua
add_rules("mode.debug", "mode.release")
includes("<nanoc SDK>/rules/nanoc/toolchain.lua")       -- toolchain + 规则
includes("<nanoc SDK>/rules/nanoc/driver/xmake.lua")    -- 独立编译驱动（推荐）

target("app")
    set_kind("binary")
    set_toolchains("nanoc")
    add_rules("nanoc.toolchain")   -- .nc 的按文件构建规则（见下"为何还要 add_rules"）
    add_deps("ncc-separate")       -- 现场构建驱动；不写则按 NANOC/PATH 探测
    add_files("src/**.nc")
```

```console
$ xmake -y
```

不写 `set_toolchains("nanoc")` 也能构建（规则会仿 dlang 自动补挂该
toolchain）；显式写出是 PRD R8 要求的正式形态标记。

## 工作机制（二期编译模型，PRD R7 独立编译 + R8 按文件产 obj）

```
src/*.nc ──ncc-separate（每文件独立调用，R7 轻装载）──► .gens/<t>/.../<file>.c
         ──内置 C 工具链编译（每文件 .c → 各自 .obj）──► .objs/.../*.obj
         ──目标级 C 链接（跨模块符号按符号名解析）──► exe
```

- **每文件一份自洽 `.c`**：`ncc-separate` 走 R7 的 `loadStandalone` 轻装载
  （#54）——编译单元 = 本模块声明 + 依赖模块导出签名的合成 `extern` 声明。
  产出的 `.c` 里，被 import 的符号只有 `extern` 原型，函数体只有本模块的；
  因此各 `.obj` 之间无重复定义，**这正是"按文件产 obj"能走 C 链接的前提**。
- **跨模块符号经 C 链接器解析**：`math.obj` 里的 `square` 定义、`main.obj`
  里的 `extern int32_t square(int32_t)` 原型，链接期按符号名配对，无需任何
  NanoC 侧链接器。
- **按文件增量**：规则用 `on_buildcmd_file`（单数，xmake 自带 cppfront 规则
  同款范式）把每个 `.nc` 的"产 `.c` + 编 `.obj`"注册为 jobgraph 独立节点，
  增量键 = 该 `.nc` 自身。改 `math.nc` 只重编 math + 重新链接，`main.nc`
  不动。
- **部分工具链**：`toolchain("nanoc")` 仿 xmake 自带 nasm——不定义 cc/ld，
  中间 `.c` 的编译与最终链接复用当前平台默认 C 工具链（xmake 对部分工具链
  自动追加平台 standalone 工具链，MSVC/gcc 环境探测与链接环境全部照常）。

## ncc-separate 驱动（为何不是 ncc 本体）

R7 的独立编译轻装载只交付了 API（#54），ncc 主 CLI 的
`ncc <file> --emit=c` 仍走整体装载 `load()`——import 闭包的**定义**会被并入
每份产物，两模块各自产 `.c` 后 C 链接报重复定义（实测 LNK2005/LNK1169）。
#54 注明 CLI 旗标接线"下一轮统一做"；R8 在 `rules/nanoc/driver/`（SDK 的
rules 区，非 ncc/**）以独立驱动 `ncc-separate` 先行接线：

- 复用 ncc 公开组件（loader/semantic/ir/c_backend），管线与
  `tests/test_separate.cpp` 的 `compileStandalone` 原型同构，走 `--emit=c` 路径；
- 未来 ncc 主 CLI 加 `--separate` 旗标后，toolchain 可切换到 `ncc --separate`，
  本驱动退役。

### 让 xmake 找到 ncc-separate（按优先级）

| 优先级 | 来源 | 说明 |
|---|---|---|
| 1 | 环境变量 `NANOC` | 指向**含 ncc-separate 的目录**，或直接指向 ncc-separate 可执行文件；无效（如指向 ncc 主程序）只警告并继续探测——同一工程常把 `NANOC` 设给 rule 形态，两形态并存时不互相卡死 |
| 2 | 工程内 target `ncc-separate` | include `rules/nanoc/driver/xmake.lua` 现场构建（样例的做法）；配合 `add_deps` + `build.fence` 保证先建驱动后编 `.nc` |
| 3 | `PATH` 查找 | `lib.detect.find_tool`，绕过持久检测缓存 |
| 4 | 都找不到 | 构建报错，打印完整指引 |

- 探测发生在构建期：`xmake f`（config）、`xmake run` 等动作不要求驱动存在。

## 为何还要 `add_rules("nanoc.toolchain")`

go/dlang 这类"只 `set_toolchains` + `add_files`"的语言依赖 xmake 内置
language 注册表（extension → sourcekind → 自动挂规则），而该注册表只从
xmake 安装目录的 `languages/` 加载，**项目级无法注册自定义 language**
（`core/language/language.lua` 的 `_directory()`）。因此 `.nc` 的分类走
rule 的 `set_extensions` + 显式 `add_rules`——与 xmake 自带 cppfront/
protobuf 规则同款。

## 与 rule("nanoc") 并存：选择指南（PRD R8）

两者都注册、文件独立、互不冲突（rule 名 `nanoc` vs `nanoc.toolchain`）：

| | `rule("nanoc")`（R5，一期形态） | `toolchain("nanoc")`（R8，正式形态） |
|---|---|---|
| 编译模型 | 整体编译：全部 `.nc` 一次 `ncc --emit=c` 产**单个** `.c` | 按文件：每 `.nc` 独立产 `.c`/`.obj`（R7 语义） |
| 增量粒度 | 任一 `.nc` 变化 → 整目标重跑 ncc | 改哪个文件只重编哪个文件 |
| 链接 | 单 `.c` 自洽，C 工具链一次编译链接 | 目标级 C 链接，跨模块符号按符号名解析 |
| 依赖的编译器 | `ncc`（主 CLI） | `ncc-separate`（独立编译驱动） |
| 适用 | 简单场景、小程序、最少配置 | 多模块工程、重增量、需要与 C 混链的正式场景 |

**选择**：单文件/小程序选 rule（少一行 `add_rules`、少一个驱动依赖）；
多模块、在意增量、或要把 NanoC 产物与 C 目标真正链接的工程选 toolchain。

## 与 VM 后端形态的差异（extern/宿主函数）

| | C 后端 + toolchain 形态（本文） | VM 后端形态（nvm） |
|---|---|---|
| extern 调用 | 生成的 `.c` 是 C `extern` 原型，**走 C 原生链接**：符号从系统库或 `add_links` 解析，无需宿主库 | `CALLX` 动态导入，运行期由 nvm 宿主库提供实现（需 nvm-host-lib） |
| import/export | C 链接器按符号名解析 | NCI v2.1 导入/导出表 + `nas -r` 链接器重定位 |
| 产物 | 平台原生 exe | `.nci` 字节码，nvm 执行 |

## 已知边界（如实记录）

1. **需要 `ncc-separate`**：本仓库 `rules/nanoc/driver/` 可构建；旧版 SDK
   没有该驱动，按报错指引获取。
2. **跨模块签名漂移不触发导入方重编**：每文件的增量键 = 该 `.nc` 自身；
   改 `math.nc` 的函数**签名**不会自动重编 import 它的模块（链接按符号名
   解析，签名不匹配属未定义行为，需 clean 或手动 touch）。与 R7"签名级
   检查"边界一致。需要闭包级增量的调用方可自行使用 ncc-separate 的
   `-MMD` 依赖文件（含 import 闭包，装载顺序）。
3. **`.nc` 需显式 `add_rules`**：xmake language 注册表只在安装目录（见上），
   无法做到 go 式"零规则"体验。
4. **`add_files` 里的 `.nc` 应位于工程目录内**：生成的 `.c` 镜像源码相对
   路径铺在 target 的 autogen 目录，工程外的绝对路径文件不在保证范围。
5. **C 编译选项**：生成的 `.c` 走平台默认 C 工具链，`add_cflags`/
   `add_includedirs`/`add_links` 等照常生效于生成代码的编译与链接。
6. **CI**：本形态不进主工程 CI 路径——根 `xmake.lua` 未 glob
   `examples/toolchain_project/**` 与 `rules/**`，主工程构建/测试不受影响；
   验收用样例工程端到端（进程级验证，不入 GoogleTest）。

## 验证记录（本机，Windows x64 + MSVC 2022，xmake v3.0.9）

```console
$ cd examples/toolchain_project
$ xmake f -P . -p windows -a x64 -y && xmake -P . -y
[ 50%]: <ncc-separate> linking.release ncc-separate.exe
[ 69%]: <app> compiling.nanoc src\math.nc
[ 75%]: <app> compiling.nanoc src\main.nc
[ 82%]: <app> linking.release app.exe
[100%]: build ok, spent 10.297s
$ ./build/windows/x64/release/app.exe; echo $?
49                                    # square(add(2,5))

$ touch src/math.nc && xmake -P . -y  # 增量：只重编 math
[ 75%]: <app> compiling.nanoc src\math.nc
[ 82%]: <app> linking.release app.exe
[100%]: build ok, spent 0.516s

$ xmake clean -P . && xmake -P . -y   # 全量复建
[100%]: build ok, spent 17.563s
$ ./build/windows/x64/release/app.exe; echo $?
49
```

生成的 `main.c`（extern 注入证据，跨模块符号交 C 链接器）：

```c
// extern declarations: host-provided C functions
extern int32_t add(int32_t a, int32_t b);
extern int32_t square(int32_t x);
```

无 ncc-separate 环境（`NANOC` 未设、PATH 无驱动、工程内无驱动 target）→
构建报"未找到 NanoC 独立编译驱动 ncc-separate"及完整指引；include 驱动
target 或设 `NANOC`/PATH 后重试 → 构建通过。

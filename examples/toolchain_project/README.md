# toolchain_project —— `toolchain("nanoc")` 样例（PRD R8 / M4）

两个模块的 NanoC 工程走 **toolchain 正式形态**：每个 `.nc` 独立编译
（`ncc-separate` 单文件 `--emit=c` → 每文件 `.c` → 各自 `.obj`），目标级用
C 链接器链接。**改单个 `.nc` 只重编该文件**（文件级增量）。

- `src/math.nc`：`export` 两个纯函数（模块）
- `src/main.nc`：`import math` 后调用（入口）

## 构建与运行

在 SDK 仓库内构建需 `-P .`（xmake 向上找工程文件会命中 SDK 根工程，
见 `doc/xmake-rule.md` 的同名说明；仓库外的真实工程直接 `xmake -y`）：

```console
$ cd examples/toolchain_project
$ xmake -P . -y
[ 50%]: <ncc-separate> linking.release ncc-separate.exe
[ 69%]: <app> compiling.nanoc src\math.nc
[ 75%]: <app> compiling.nanoc src\main.nc
[ 82%]: <app> linking.release app.exe
[100%]: build ok
$ ./build/windows/x64/release/app.exe; echo $?
49
```

（`main` 以退出码承载结果：`square(add(2, 5)) = 49`。`xmake run -P . app`
 也能运行，但 xmake 把非零退出码按失败展示为 `execv ... failed(49)`，
 直接执行更直观。）

文件级增量（改一模块只重编该模块，PRD R7/R8 验收口径）：

```console
$ touch src/math.nc
$ xmake -P . -y
[ 75%]: <app> compiling.nanoc src\math.nc      # main.nc 不重编
[ 82%]: <app> linking.release app.exe
[100%]: build ok
```

## 机制

```
src/*.nc ──ncc-separate（每文件独立，PRD R7 轻装载）──► .gens/<t>/.../<file>.c
         ──内置 C 工具链（每文件 .c → 各自 .obj）──► .objs/.../*.obj
         ──目标级 C 链接（跨模块符号按符号名解析）──► app.exe
```

- 每文件的 `.c` 只含**本模块**的函数体；被 import 模块的符号以合成
  `extern` 原型出现（PRD R3 发射路径），C 链接天然解析——重复定义问题
  不存在，这正是"按文件产 obj"的前提。
- 手写 `extern` 声明调用宿主/C 函数时同样走 C 原生链接（从系统库或
  `add_links` 解析），**无需宿主库**——与 VM 后端形态（nvm 宿主库 +
  CALLX 动态导入）不同。
- 增量键 = 每个 `.nc` 自身。改 math.nc 的**函数签名**不会自动重编 import
  它的 main.nc（跨模块签名漂移边界，与 R7 一致）；需要闭包级增量可自行
  使用 ncc-separate 的 `-MMD` 依赖文件。

## ncc-separate 从哪来

`ncc-separate` 是 NanoC 独立编译驱动（R7 `loadStandalone` 的 CLI 接线，
见 `rules/nanoc/driver/`）。本样例用 `includes(.../driver/xmake.lua)` +
`add_deps("ncc-separate")` 现场构建；等价做法：

```console
# 自行从 SDK 构建后，二选一：
$ export NANOC=<nanoc 仓库>/build/windows/x64/release   # 指向含驱动的目录
$ export PATH=<nanoc 仓库>/build/windows/x64/release:$PATH
```

探测优先级（`NANOC` → 工程内 target → `PATH`）与报错指引见
`rules/nanoc/toolchain.lua`。

## 与 rule 形态（hello_project）的选择

| | rule("nanoc")（R5） | toolchain("nanoc")（R8） |
|---|---|---|
| 编译模型 | 整体编译：全部 `.nc` 一次 `ncc --emit=c` 产单个 `.c` | 按文件：每 `.nc` 独立产 `.c`/`.obj` |
| 增量粒度 | 任一 `.nc` 变化 → 整目标重跑 | 改哪个文件只重编哪个文件 |
| 链接 | 单 `.c` 自洽，C 工具链一次编译链接 | 目标级 C 链接，跨模块符号按符号名解析 |
| 依赖的编译器 | `ncc`（主 CLI） | `ncc-separate`（独立编译驱动） |
| 适用 | 简单场景、小程序 | 多模块、重增量、与 C 混链的正式工程 |

详见 `doc/xmake-toolchain.md`。

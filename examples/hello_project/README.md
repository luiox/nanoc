# hello_project — NanoC xmake 规则样例

演示用 [`rule("nanoc")`](../../../rules/nanoc/nanoc.lua) 把 NanoC 源码接入
xmake 构建：一条命令完成 `.nc → C → exe`。

## 构建运行

```console
$ cd examples/hello_project
$ xmake -y
$ xmake run hello
$ ./build/windows/x64/release/hello.exe; echo $?   # 退出码 42（add(40, 2)）
```

注意：样例放在 SDK 仓库目录内时，xmake 会向上取到 SDK 根的 `xmake.lua`
（xmake 总是选最顶层的工程文件），需显式指定工程目录：

```console
$ xmake -P . -y          # 在 SDK 仓库内构建本样例
$ xmake run -P . hello
```

把整个 `hello_project/` 拷到 SDK 仓库之外（真实用户工程的位置）后，
直接 `xmake` 即可，无需 `-P .`。

## 前置条件：让 xmake 找到 ncc

规则在构建期按以下顺序探测 NanoC 编译器 `ncc`：

1. 环境变量 `NANOC`，指向 ncc 可执行文件（或其所在目录）；
2. `PATH` 上的 `ncc`；
3. 都找不到则构建报错，并打印上述指引。

在 NanoC SDK 仓库构建出 ncc 后，最省事的接法是把它指给本工程：

```console
# 在 NanoC SDK 仓库根目录
$ xmake build ncc

# 回到本样例目录，任选其一
$ export NANOC=<nanoc 仓库>/build/windows/x64/release/ncc.exe   # Windows (Git Bash 语法)
$ set NANOC=<nanoc 仓库>\build\windows\x64\release\ncc.exe      # Windows (cmd)
$ export PATH=<nanoc 仓库>/build/windows/x64/release:$PATH      # 或挂到 PATH
```

> 注意：`--emit=c` 需要 PRD R3/R4（extern 声明 + C 后端 CLI 接线）合入后的
> ncc；旧版 ncc 会报 `--emit=c is not supported yet`。

## 工程形态

```lua
-- xmake.lua
includes("../../rules/nanoc/nanoc.lua")  -- 引入 SDK 规则文件

target("hello")
    set_kind("binary")
    add_rules("nanoc")
    add_files("src/**.nc")
```

`src/main.nc` 是纯计算程序（用户函数 + 返回值，无外部依赖），以退出码承载
结果。多模块写法见 `doc/xmake-rule.md`（跨文件调用需 `export`）。完整接入
说明与已知边界同见 `doc/xmake-rule.md`。

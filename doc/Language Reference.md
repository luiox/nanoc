# NanoC 语言规范（NanoC Language Specification）

**版本：v1.0**（对应实现：ncc 0.1.0，main@7cc576f 一线）

本规范以**当前实现为准**逐条提炼：词法/文法对照 `ncc/src/ncc/`（lexer/parser/ast/semantic/ir），
语义对照三个后端（NAS/VM、C、LLVM）与 `nvm`。凡实现与本文不一致处，以实现为现状记录，
并列入规范 PR 的"文档-实现不一致清单"供后续修复。

规范性措辞约定（参照 ISO C / Rust Reference）：

- **必须 / 不得**：强制要求，违反即编译期错误（除非明确标注为未定义行为）；
- **应当**：强烈建议，违反不构成错误；
- **未定义行为（UB）**：本规范不对其运行结果作任何要求；
- **实现定义**：多个合法取值中，由实现选定并在本文档明示；
- **裁定**：本文档对一处 C 语义未定点的明确选择（教学语言原则：能裁定的不留给 UB）。

每条语义均配最小示例；全部示例均经三后端真机验证（验证清单见 §11.2）。

---

## 目录

1. [前言](#1-前言)
2. [词法](#2-词法)
3. [文法](#3-文法)
4. [类型系统](#4-类型系统)
5. [语义](#5-语义)
6. [扩展特性：defer、match、coro/yield](#6-扩展特性defermatchcoroyield)
7. [与 C 互操作](#7-与-c-互操作)
8. [可观测行为与工具链契约](#8-可观测行为与工具链契约)
9. [未定义行为与实现定义行为清单](#9-未定义行为与实现定义行为清单)
10. [版本与演进](#10-版本与演进)
11. [附录](#11-附录)

---

## 1 前言

### 1.1 语言定位

NanoC 是一个**教学型 C 子集**：保留 C 的核心执行模型（函数、标量类型、指针、数组、
struct、递归、手写控制流），剥离工程化复杂度（无浮点、无位运算、无多级指针、无多维
数组、无显式转换、无宏系统），并在此子集上叠加三个**受现代语言启发的扩展特性**：
`defer`（Go）、`match`（Rust/Swift）、`coro/yield`（无栈协程）。

设计目标（摘自 PRD《多后端编译与语言特性》）：

1. **一套前端与 IR，三个后端并存**：`ncc` 将 NanoC 源编译为统一 IR（`ir::Module`），
   三个后端独立消费同一 IR——NAS/VM 后端（默认，产物经 `nas` 汇编、`nvm` 执行）、
   C 后端（`--emit=c`，可读 C 源）、LLVM 后端（`--emit=llvm|obj|exe`，文本 `.ll` 经
   `llc` 编译链接）。
2. **差分一致性**：同一程序在三个后端上必须产生一致的可观测行为（§8.4）。
3. **教学友好**：诊断一律 `file:line:col`；能用编译期裁定回答的问题不留运行期悬念。

### 1.2 与 C 的关系

NanoC = **C 的一个子集 + 少量超集**：

| 维度 | 关系 |
|---|---|
| 类型 | 子集：`int`/`char`/`void`/一级指针/一维数组/`struct`/`typedef`；无 `unsigned`/浮点/位域/union/enum（头文件互操作中按裁定折叠，§7.2） |
| 表达式 | 子集：无逗号/条件/位运算/复合赋值/自增自减/`sizeof`/强制转换 |
| 语句 | 子集：无 `switch`/`do-while`/`goto`/标号/空语句 |
| 程序组织 | 超集：`import`/`export`（文件即模块）；`extern` 声明与 `#include "x.h"` 声明子集用于 C 互操作 |
| 扩展特性 | 超集：`defer`、`match`、`coro`/`yield` |

### 1.3 一个最小程序

```nc
int main() {
    return 0;
}
```

`main` 是程序入口（§5.6）；其返回值即程序退出码的来源（§8.3）。

---

## 2 词法

### 2.1 字符集

- 源文件**必须**为 UTF-8 编码（无 BOM 即可；MSVC 构建由 `/utf-8` 统一源字符集与执行字符集）。
- 词法器按字节处理输入；诊断中的列号按**字节**计（多字节 UTF-8 字符使列号偏大），
  行号从 1 起、列号从 1 起。
- 标识符与关键字只接受 ASCII 字母；非 ASCII 字符只能出现在注释与字符串字面量中。

### 2.2 注释

- 行注释：`//` 起至行尾。
- 块注释：`/*` 至 `*/`；**不得嵌套**。

```nc
// 行注释
/* 块注释
   第二行 */
int x = 1; // 行尾注释
```

### 2.3 关键字

以下 20 个标识符拼写为关键字，不得用作标识符：

```
int    char     void    if      else    while   for       return
break  continue NULL    struct  typedef import  export    extern
defer  match    coro    yield
```

> `NULL` 是关键字而非宏（§2.6）。`_` 经标识符通道词法化，是通配模式的保留拼写
> （§6.2）：**程序不得**将其用作变量名（当前实现不在编译期诊断此误用，
> 属规范先于诊断的约束）。

### 2.4 标识符

```
identifier ::= [A-Za-z_][A-Za-z0-9_]*
```

无长度上限；大小写敏感。

### 2.5 整型字面量

```
integer-literal ::= [0-9]+
```

- **只有十进制**。不支持十六进制/八进制/二进制前缀，不支持任何后缀（`L`、`U` 等）。
- 负值是字面量上前的一元 `-` 运算符（§3.4），不是字面量的一部分。
- 字面量的值必须可由 32 位有符号整数表示；超出 `[-2147483648, 2147483647]` 的
  十进制串（考虑前导一元 `-` 后越界的正值字面量）为**编译期错误**。

```nc
int a = 42;    // OK
int b = 0x1F;  // 错误：不支持十六进制
int c = 100L;  // 错误：不支持后缀
```

### 2.6 字符字面量与字符串字面量

```
char-literal   ::= '\'' [任意单个源字节] '\''
string-literal ::= '"' [字符串字符]* '"'
```

**字符字面量**：

- 恰好容纳**单个源字节**，**不支持转义序列**：`'\n'`、`'\''`、`'\\'` 均为词法错误。
- 值为该字节的字节值；字节值 128..255 的符号解释见 §9.4（应避免使用非 ASCII 字符字面量）。

```nc
char a = 'A';   // OK，值 65
char b = '\n';  // 词法错误：字符字面量不支持转义
```

**字符串字面量**：

- 类型为 `char*`（§4.5），值是数据段中按字节打包、以 NUL 终止的常量对象地址。
- 支持转义序列：`\n` `\t` `\r` `\0` `\\` `\"`。其余转义序列（如 `\x41`）行为
  按后端而异（NAS 汇编侧报错），**不得**使用。
- 字符串字面量**不得跨行**；未闭合为词法错误。
- 字符串字面量对象为只读（§9.5）。

```nc
extern int puts(char* s);
int main() {
    puts("hello\n");  // OK
    return 0;
}
```

### 2.7 运算符与标点

| 类别 | 记号 |
|---|---|
| 算术 | `+` `-` `*` `/` `%` |
| 关系 | `==` `!=` `<` `<=` `>` `>=` |
| 逻辑 | `&&` `||` `!` |
| 指针/成员 | `&`（取址） `*`（解引用，与乘法按上下文区分） `.` `->` |
| 赋值 | `=` |
| match | `=>`（分支引导） `..`（闭区间） |
| 可变参数 | `...`（仅 extern 声明） |
| 标点 | `;` `,` `(` `)` `{` `}` `[` `]` |

**不得出现**的语言记号（词法层直接报未知记号或解析错误）：`++` `--` `+=` 等复合赋值、
`?:`、`&` `|` `^` `<<` `>>` 位运算、`::` 等。

---

## 3 文法

### 3.1 记法

以下 EBNF 从 `ncc/src/ncc/parser.cpp` 逐一对照提炼；`*` 重复、`?` 可选、`|` 分支。
`"x"` 为终结符拼写。语义约束（如"数组大小必须为正"）不在文法内表达，见引用章节。

### 3.2 翻译单元与声明

```
translation-unit ::= { import-directive | declaration }

import-directive ::= "import" ( identifier | string-literal ) ";"
```

- **约束**：`import` 只允许出现在文件顶部（任何声明之前）；声明之后出现 `import`
  为编译期错误。
- `import math;` 导入导入者同目录的 `math.nc`；`import "util/helpers.nc";` 按
  相对导入者目录的显式路径导入（§7.4）。

```
declaration ::= export? coro? function-definition
              | export? coro? var-declaration
              | struct-declaration
              | typedef-declaration
              | extern-declaration

export ::= "export"    -- 修饰顶层函数/全局变量；重复、修饰 extern/struct/typedef 为错误
coro   ::= "coro"      -- 修饰函数；只接受 "export coro" 顺序，"coro export" 为错误
```

### 3.3 声明

```
function-definition ::= type-spec pointer* identifier "(" parameter-list? ")" compound-statement

parameter-list      ::= parameter { "," parameter }
parameter           ::= type-spec pointer* identifier
```

- 数组形参不支持：`int f(int a[10])` 为编译期错误；数组实参传给指针形参即完成退化（§4.6）。

```
var-declaration ::= type-spec pointer* identifier array-suffix? ("=" initializer)? ";"
array-suffix    ::= "[" integer-literal "]"      -- 一维；多维在此解析、由语义拒绝
initializer     ::= expression | brace-init-list
brace-init-list ::= "{" [ expression { "," expression } ] "}"   -- 仅完整 struct 声明
```

```
struct-declaration ::= "struct" identifier "{" { struct-field } "}" ";"    -- 定义
                     | "struct" identifier ";"                              -- 前向声明
struct-field       ::= type-spec pointer* identifier array-suffix? ";"
```

- struct 定义只允许出现在文件作用域；字段不得带初始化器；字段不得为 `void` 或
  不完整 struct 值；`typedef` 不得出现在 struct 体内（§4.7）。

```
typedef-declaration ::= "typedef" ( type-spec | "struct" identifier | anonymous-struct )
                        pointer* identifier ";"
anonymous-struct    ::= "struct" "{" { struct-field } "}"
```

- 数组 typedef 不支持；typedef 只允许出现在文件作用域（§4.8）。

```
extern-declaration ::= "extern" type-spec pointer* identifier "(" extern-params? ")" ";"
extern-params      ::= extern-param { "," extern-param } [ "," "..." ] | "..."
extern-param       ::= type-spec pointer* identifier
```

- `...` 必须是最后一个参数，其后不得再出现命名参数；extern 声明不得有函数体（§7.1）。

### 3.4 语句

```
statement ::= compound-statement
            | if-statement | while-statement | for-statement
            | return-statement | break-statement | continue-statement
            | var-declaration-statement
            | defer-statement | yield-statement
            | expression-statement

compound-statement        ::= "{" { statement } "}"
if-statement              ::= "if" "(" expression ")" statement [ "else" statement ]
while-statement           ::= "while" "(" expression ")" statement
for-statement             ::= "for" "(" [ statement ] ";" [ expression ] ";"
                              [ expression ] ")" statement
return-statement          ::= "return" [ expression ] ";"
break-statement           ::= "break" ";"
continue-statement        ::= "continue" ";"
var-declaration-statement ::= var-declaration      -- 同 3.3 局部形态
defer-statement           ::= "defer" expression-statement
yield-statement           ::= "yield" expression ";"
expression-statement      ::= expression ";"
```

- **无空语句**：单独的 `;` 不是合法语句。
- `for` 的 init 槽位是一条语句（声明或表达式语句）；条件与步进可为空，
  空条件视为恒真（`for (;;)` 为合法死循环写法）。
- `if` 的 `else` 与最近的未匹配 `if` 结合（悬垂 else 标准规则）。
- `typedef` 与 `extern` 声明只允许文件作用域，出现在语句位置为编译期错误。
- `yield` 仅 `coro` 函数体内合法（§6.3）。

### 3.5 表达式与优先级

按优先级**从低到高**（同层左结合，除赋值右结合）：

| 级 | 类别 | 运算符 | 结合性 |
|---|---|---|---|
| 1 | 赋值 | `=` | 右 |
| 2 | 逻辑或 | `||` | 左 |
| 3 | 逻辑与 | `&&` | 左 |
| 4 | 相等 | `==` `!=` | 左 |
| 5 | 关系 | `<` `<=` `>` `>=` | 左 |
| 6 | 加减 | `+` `-` | 左 |
| 7 | 乘除模 | `*` `/` `%` | 左 |
| 8 | 一元（前缀） | `-` `!` `&` `*` | 右 |
| 9 | 后缀 | `[ ]`（下标） `.` `->`（成员） | 左 |
| 10 | 初等 | 字面量、`NULL`、标识符、`f(args)`、`( expression )`、`match` 表达式 | — |

```
assignment     ::= logical-or [ "=" assignment ]
logical-or     ::= logical-and { "||" logical-and }
logical-and    ::= equality { "&&" equality }
equality       ::= relational { ( "==" | "!=" ) relational }
relational     ::= additive { ( "<" | "<=" | ">" | ">=" ) additive }
additive       ::= multiplicative { ( "+" | "-" ) multiplicative }
multiplicative ::= unary { ( "*" | "/" | "%" ) unary }
unary          ::= ( "-" | "!" | "&" | "*" ) unary | postfix
postfix        ::= primary { "[" expression "]" | "." identifier | "->" identifier }
primary        ::= integer-literal | char-literal | string-literal | "NULL"
                 | identifier | identifier "(" argument-list? ")"
                 | "(" expression ")" | match-expression
argument-list  ::= expression { "," expression }
```

- **赋值目标受限**：只有标识符（`x = v`）、下标（`a[i] = v`）、解引用（`*p = v`）、
  成员（`p.x = v` / `p->x = v`）四种左值形式可赋值；其余目标为编译期错误。
- 赋值是表达式，右结合：`a = b = 1` 合法。
- `*p[i]` 按后缀优先解析为 `*(p[i])`（与 C 一致）。

---

## 4 类型系统

### 4.1 类型总览

| 类型 | 拼写 | 语义 |
|---|---|---|
| 布尔上下文 | — | 无独立布尔类型；标量/指针非零为真（§5.4） |
| 整型 | `int` | 32 位二进制补码有符号整数（**裁定：即 i32**） |
| 字符型 | `char` | 8 位（§4.2） |
| 空类型 | `void` | 仅作函数返回类型；`void*`、`void` 变量/字段/数组均非法 |
| 指针 | `T*` | 一级指针，指向 T；多级指针非法 |
| 数组 | `T[n]` | 一维、定长（n 为十进制整型字面量，必须 > 0） |
| 结构体 | `struct Tag` | 值语义聚合类型 |
| 别名 | `typedef` | 透明别名，与被别名类型同一 |
| 空指针常量 | `NULL` | 可赋给/比较任意指针类型的空指针常量（独立类型态，非 `int`） |

**无浮点（裁定与理由）**：语言不提供 `float`/`double`。理由：教学定位聚焦整数执行
模型；VM 字节码与 NAS 后端为纯整数指令集，引入浮点需三后端同步改造 ABI 与差分口径，
成本远超教学收益。头文件互操作中的浮点声明显式报错而非静默折叠（§7.2）。

### 4.2 int 与 char 的宽度（钉死）

- **`int` 是 32 位二进制补码有符号整数**。三后端映射：
  - NAS/VM：一个 32 位字（VM 寄存器/内存字宽均为 32 位）；
  - C 后端：C `int`（宿主 LP64/LLP64 下均为 32 位）；
  - LLVM 后端：`i32`。
- **`char` 是 8 位**。三后端映射：
  - NAS/VM：char **值**以一个 32 位字存储（VM 无亚字寻址）；char 数组按每元素一字存储；
  - C 后端：C `char`；
  - LLVM 后端：`i8`（有符号，C `char` 语义）。
  - **例外**：字符串字面量在数据段中按**字节打包**（每字节一格），因此 `char*`
    不得解引用或下标（§4.5）——这是类型系统层面的裁定，不是 UB。
- char 参与算术时提升为 int（§4.9）；int 到 char 无隐式窄化（§4.9）。

### 4.3 struct 布局

- 成员按声明顺序排布；**VM 模型：4 字节对齐、无填充**——每个标量/指针成员占 4 字节
  （char 亦然），struct 大小 = 成员大小之和（数组成员按 元素数 × 元素大小）。
- **原生后端布局差异（实现定义）**：C/LLVM 后端使用宿主自然布局（`char` 1 字节、
  指针 8 字节）。当自然布局与 VM 布局一致时，C 后端以 `_Static_assert` 钉死成员偏移；
  不一致时后端发出布局分歧说明，该模块在自身布局下自洽。**跨后端读取 struct 内部
  字节布局的程序不得依赖 `sizeof`/偏移的具体值**；成员访问（`p.x`/`p->x`）始终按
  各自后端布局正确执行。
- 空 struct（无成员）非法；struct 不得定义于函数体内；自引用只能经指针成员
  （`struct Node* next`），值成员不得为不完整类型。

### 4.4 指针

- 指针类型为 `T*`，T 为标量或 struct；**多级指针（`int**`）非法**；指针数组
  （`int* a[4]`）非法；`void*` 非法。
- 指针宽度（实现定义）：VM 模型 4 字节；原生后端为宿主指针宽度（x86_64 下 8 字节）。
- `NULL` 可赋给任意指针类型、可与任意指针比较；解引用 `NULL` 是 UB（§9.6）。
- 指针支持的运算：解引用 `*`、取址 `&`（仅左值；数组名不可取址——其已可退化）、
  下标 `p[i]`（等价 `*(p+i)`，指针算术按指向类型大小缩放）、成员 `p->x`、
  与同型指针或 `NULL` 的 `==`/`!=`/关系比较、`p + n` / `p - n`（n 为标量）。
- **指针减法（`p - q`）非法**（无 ptrdiff 类型）；指针与整数互转非法（无强制转换语法）；
  不同指向类型的指针互转非法。

### 4.5 数组

- `T a[n]`：一维、定长、`n` 为正的十进制整型字面量；元素类型 T 为标量或 struct
  （char 数组每元素在 VM 上占一字，§4.2）。
- **数组不可整体拷贝**：初始化/赋值/传参中出现数组到数组的拷贝为编译期错误；
  **数组初始化器（`int a[3] = {1,2,3};`）不支持**。
- **数组到指针退化**：数组名在传参、算术、比较等使用点退化为指向首元素的指针
  （`Array(T)` → `T*`）；`&a` 非法。
- 数组元素访问 `a[i]` 不做边界检查（越界为 UB，§9.7）。
- 数组不得作函数形参（声明即错误）； struct 可含数组成员。

### 4.6 隐式转换规则（穷尽列举）

设源类型 S、目标类型 D：

| S → D | 规则 |
|---|---|
| 同一类型 | 合法（struct 同标签逐字拷贝；数组到数组非法） |
| `char` → `int` | 隐式加宽，值保持（有符号） |
| `int` → `char` | **非法**（无显式转换语法，禁止静默截断——裁定） |
| `Array(T)` → `T*` | 数组退化（仅使用点） |
| `NULL` → 任意 `T*` | 合法 |
| 其余一切 | **非法**：指针↔标量、不同指针间、NULL↔标量、struct↔标量、不同 struct 间 |

无强制转换运算符（`(int)x` 语法不存在）。窄化的唯一替代路径：经 char 数组元素
逐个赋值，或经 extern 宿主函数。

```nc
int main() {
    char c = 'A';     // char 字面量 → char：OK
    int i = c;        // char → int 加宽：OK
    c = i;            // 错误：不能隐式把 int 转成 char
    return 0;
}
```

### 4.7 struct 与 typedef 补充规则

- struct 与 typedef **只允许文件作用域**；类型命名空间为**全编译单元**（不强制
  文本先序：`struct Node;` 前向声明 + 字段引用即可；同单元内先使用后定义的标签合法）。
- 类型命名空间与变量/函数命名空间分离：typedef 别名与 struct 标签共享一个命名空间，
  不得重名；与变量/函数重名合法。
- typedef 形态：`typedef int T;`、`typedef int* P;`、`typedef struct Point PointT;`、
  `typedef struct { ... } Anon;`（匿名定义）、`typedef struct Tag { ... } T;`（定义+别名一体）、
  `typedef struct X X;`（幂等自引用别名，合法）。

### 4.8 类型查询

无 `sizeof`、无 `typeof`。类型宽度以 §4.2/§4.3/§4.4 的钉死规则为准。

---

## 5 语义

### 5.1 作用域

- **文件作用域**：struct/typedef（类型命名空间，全编译单元可见）；函数签名
  （先注册后检查，调用点可前向引用，先调用后定义、相互递归均合法）；
  全局变量（**先声明后可见**——声明顺序即可见顺序）。
- **块作用域**：每个复合语句一个作用域；内层声明遮蔽外层同名声明；同层重声明为
  `redefinition` 错误。函数形参与函数体最外层块共用一个作用域（形参与顶层局部同名
  冲突）。
- **for 作用域**：for 整体（含 init 声明、条件、步进、体）构成一个作用域；init 中
  声明的变量不泄漏到循环外。
- 函数与变量共享命名空间：同一作用域内函数与变量不得重名；**无重载**。
- match 守卫绑定（§6.2）与 defer 捕获临时是编译器注入的块作用域符号，用户不得感知。

### 5.2 生命周期与存储

- **全局变量**：静态存储期；程序启动前完成初始化；**未初始化全局变量的值为 0
  （裁定，VM 数据段以零填充发射；C/LLVM 后端为零初始化静态对象）**。
- **局部变量**：自动存储期（VM：栈帧槽位；原生：栈对象）；进入作用域时创建，
  退出时销毁。**未初始化局部变量的值不确定（UB，§9.1）——实现不为局部变量清零**。
- **struct 值语义**：struct 变量的初始化/赋值/传参/返回均为**逐成员拷贝**。
- 无动态分配（无 malloc/new）；无地址算术（除 `p ± n`）。

### 5.3 初始化

- 标量/指针：`=` 表达式初始化，按 §4.6 转换规则检查。
- struct：`=` 同型 struct 表达式，或 `{ e1, ..., en }` **逐成员扁平初始化器**——
  值的个数必须等于成员数（多、少均错误）；不得嵌套；仅完整 struct 变量可用；
  数组与标量目标的初始化列表非法。
- 初始化器中的 `match` 表达式非法（`match` 只能在函数体内语句位置出现，§6.2）。

### 5.4 表达式求值

- **真值判定**：`int`/`char`/指针/`NULL` 均可作条件；**非零（非空）为真**；
  `void` 值与 struct 值作条件为编译期错误。
- **短路求值**：`&&` `||` 保证短路（左侧为假/真时右侧不求值）；结果类型 `int`
  （真 = 1，假 = 0）。
- **比较运算**：结果类型 `int`（1/0）；标量间比较（char 提升）；同型指针间比较；
  指针与 `NULL` 比较；不同指向类型的指针比较非法。
- **算术运算**：`+ - * / %` 仅标量（char 提升为 int，结果 int）；`+`/`-` 另支持
  `指针 ± 标量`（按指向类型大小缩放，§4.4）；有符号溢出与除零见 §9.2/§9.3。
- **赋值表达式**：值为赋值后左值所具有的值，类型为左值类型（`a = b = 1` 右结合）。
- **一元运算**：`-x`（标量，结果 int）、`!x`（条件类型，结果 int）、`&lvalue`
  （结果 `T*`）、`*pointer`（结果 T；`char*` 解引用为编译期错误，§4.2）。
- **求值顺序**：二元操作符两侧操作数、赋值两侧、函数实参之间的相对求值顺序
  **未指定**（当前实现在所有后端按源顺序求值，程序不得依赖——裁定）。
- **函数名不是值**：无函数指针；函数名作值使用、对函数取址均为编译期错误。

### 5.5 函数调用与递归

- 实参按**值传递**（struct 逐成员拷贝传值）；形参在函数作用域内如同局部变量
  （可重新赋值，不影响实参）。
- 实参个数必须与形参严格一致；extern 可变声明（`...`）实参数 ≥ 命名形参数，
  可变部分实参须为标量/指针（§7.1）。
- **递归**允许（直接/相互递归均合法，函数签名先行注册）。
- 返回：`return expr;` 按 §4.6 转换到返回类型；**非 void 函数必须显式 return 值**，
  且函数体必须**可证明必然返回**（保守判定：块的最后一条语句必然返回，或
  `if/else` 两分支均必然返回；循环/break/continue 不提供保证）——否则编译期错误
  `missing return`。void 函数 `return;` 合法、带值非法。
- struct 可作返回类型（按值返回拷贝）。

### 5.6 程序入口

- `main` 是程序入口函数，在**全编译单元内必须唯一**（跨文件多个 `main` 为错误）。
- VM：入口点执行 `main`，`main` 返回值载入 R0（§8.3）。
- 原生后端：`main` 映射为宿主 `main`，返回值即进程退出码。
- `main` 的形参（即使声明）不接收任何命令行参数（无 argv 机制，裁定：忽略）。

### 5.7 多文件程序组织（import/export）

- `import math;` / `import "util/helpers.nc";`：装载器递归并入依赖（跨根重复 import
  幂等；环形 import 报错），全部输入合并为一个编译单元。
- `export` 修饰顶层函数或全局变量：导出名全编译单元唯一；未导出的顶层符号为
  所在文件私有（跨文件同名私有符号合法，经标号改名隔离）。
- 可见性解析：使用点先查当前文件定义，再查任意文件的导出定义；引用未导出的
  他文件私有符号报错并提示定义位置。
- 独立编译（`rules/nanoc` 的 `ncc-separate` 驱动 + `nas -r` 链接）遵循同一可见性
  契约：导出符号经 NCI v2.1 导出表跨模块解析（格式权威见
  《Bytecode Format Specification v2.1》）。

```nc
// ---- math.nc ----
export int add(int a, int b) { return a + b; }
int secret() { return 1; }          // 私有
// ---- main.nc ----
import math;
int main() {
    return add(1, 2);               // OK：3
    // return secret();             // 错误：'secret' 在 'math.nc' 中定义但未导出
}
```

---

## 6 扩展特性：defer、match、coro/yield

### 6.1 defer（作用域退出逆序执行）

**语法**：`defer expression-statement`（defer 体必须是表达式语句）。

**语义**：

1. `defer` 将其体的**执行**推迟到**所在作用域退出**时；退出包括：块正常结束、
   `return`、`break`、`continue`（经这些出口离开所有含 defer 注册的作用域时，
   按注册的**逆序**执行）。
2. **注册时求值（值捕获，Go 语义）**：defer 体表达式中的操作数在 `defer` 语句
   执行处求值捕获；推迟到作用域退出时才发生的只有**顶层调用本身**——顶层调用的
   实参在注册点捕获，其余形态（赋值、一般表达式）整体值捕获后在退出时重放。

```nc
int main() {
    int x = 1;
    {
        defer x = 99;   // 赋值整体值捕获：注册时读 x（=1），退出时写 99
        x = 2;          // 不影响已捕获的值
    }                   // 退出块：x 被写为 99
    return x;           // 99
}
```

3. **限制**（均为编译期错误）：
   - defer 体内不得出现 `return`、`break`、`continue`、`defer`；
   - defer 体必须是表达式语句（块、if、声明等形态拒绝）；
   - `yield` 不得出现在存在 pending defer 的作用域内（§6.3）。

4. defer 展开在 IR 层完成（所有退出点逆序插入），三个后端零改动获得一致语义。

### 6.2 match（模式匹配表达式）

**语法**：

```
match-expression ::= "match" "(" expression ")" "{" match-arm { "," match-arm } "}"
match-arm        ::= pattern-list "=>" ( expression | compound-statement )
pattern-list     ::= match-pattern { "," match-pattern }
match-pattern    ::= [ "-" ] constant            -- 常量（整型/字符字面量，可带负号）
                   | [ "-" ] constant ".." [ "-" ] constant   -- 闭区间（含端点）
                   | identifier "if" expression  -- 守卫绑定
                   | "_"                          -- 通配
```

> 分支间 `,` 可省略亦可携带（允许尾逗号）；`constant` 指整型字面量或字符字面量
> （§2.5/§2.6）。

**语义**：

1. 主体表达式**只求值一次**；类型必须是 `int` 或 `char`（标量；字符串/指针/struct
   主体非法）。
2. 分支**按源顺序**逐个尝试；命中即求值分支体并以其值为 match 表达式的值；
   常量/区间模式按值匹配；多值模式（`10, 11 => ...`）为多个模式的 OR；
   守卫绑定 `n if cond` 将主体值绑定到 `n`（类型同主体），`cond` 为真时命中；
   `_` 匹配一切。
3. **无分支命中时 match 表达式的值为 0**（裁定；非错误）。
4. **穷尽性**：无 `_` 分支不构成错误，仅产生**编译期警告**
   `match has no wildcard ('_') arm; unmatched values produce 0`；守卫不视作穷尽。
   `_` 之后的分支永不可达，产生警告 `unreachable match arm after wildcard`。
5. 分支体为**表达式**形态时类型必须是 int/char；为**块**形态时允许任意副作用语句，
   块形态的 match 值为 0。match 表达式结果类型恒为 `int`。
6. match 只能出现在**函数体内的语句位置**（作为表达式语句、初始化、赋值右侧等）；
   全局初始化器中的 match 非法。
7. 区间模式为**闭区间**（含两端，裁定）；下界 > 上界为编译期错误。

```nc
int classify(int n) {
    return match (n) {
        0 => 10,
        1..9 => 20,          // 闭区间
        10, 11 => 30,        // 多值
        n if n < 0 => 40,    // 守卫绑定
        _ => 0,              // 通配（尾逗号允许）
    };
}
int main() { return classify(-5) + classify(3) + classify(99); }  // 40+20+0 = 60
```

### 6.3 coro/yield（无栈协程）

**语法与内建**：

```nc
coro int squares(int n) {
    for (int i = 0; i < n; i = i + 1) { yield i * i; }
    return -1;                       // 完成态返回值（约定 -1 表示结束）
}
int main() {
    int h = coro_create(squares, 5); // 创建实例，返回句柄（int）
    int v = coro_resume(h);          // 驱动至下一个 yield：0
    v = coro_resume(h);              // 1
    int done = coro_done(h);         // 0（未完成）/ 1（已完成）
    return 0;
}
```

**模型（裁定 A2：无栈协程 = IR 层状态机变换）**：coro 函数经编译器变换降解为
「帧 struct + 状态机函数」；`yield v;` 降解为「存恢复点 + `return v;`」；
`coro_create` 在调用点内联展开为帧槽分配 + 参数落帧。三个后端零改动获得一致语义。

**规则**：

1. `coro` 只能修饰函数；coro 函数**返回类型必须是 `int`**（一期裁定）。
2. coro 函数**不得直接调用**——只能经 `coro_create` 取句柄、经 `coro_resume` 驱动
   （直接调用为编译期错误）。
3. `yield` 必须带值（裸 `yield;` 为错误）；`yield` 只允许出现在 coro 函数体内。
4. **yield × defer 互斥**：`yield` 所在作用域链上存在 pending defer 时为编译期错误
   （挂起/恢复会绕过 defer 的退出点展开，组合语义不可良定义——硬约束裁定）。
5. **帧提升契约**：跨 yield 存活的参数与局部变量只能是标量（int/char/指针）；
   struct/数组类型的 coro 参数与局部变量为编译期错误。
6. **句柄**：`h = fnid * 16 + slot`（fnid 为单元内 coro 声明序，slot 为实例槽号）。
   一期限制（裁定）：**每 coro 函数最多 16 个并发实例**；槽位 bump 分配、**不回收**
   （超出后 `coro_create` 的行为为 UB，§9.8）；句柄只是 int，不校验归属。
7. **resume-after-done 幂等**：实例完成后 `coro_resume(h)` 幂等返回其完成态返回值
   （`return` 值；fall-off-end 等价 `return 0`）；`coro_done(h)` 对完成实例恒为 1。
8. `coro_create`/`coro_resume`/`coro_done` 是**内建保留名**，不得定义或 extern 同名
   符号；`coro_create` 的第一实参必须是**本文件内定义**的 coro 函数名（编译期解析，
   非函数指针——语言无函数类型）。
9. coro 内建只能在函数体内使用（全局初始化器中非法）。

---

## 7 与 C 互操作

### 7.1 extern 声明

**语法**（§3.3）：`extern int puts(char* s);`、`extern void exit(int);`、
`extern int printf(char* fmt, ...);`

**规则**：

- 只允许文件作用域；不得有函数体；不得被 `export`/`coro` 修饰。
- `...` 可变参数只允许出现在形参表末尾。
- 同名 extern 与 extern 的重复声明冲突规则相互独立（重复 extern 同名符号按
  重定义处理；头文件原型合并语义见 §7.2）。
- 调用 extern 函数：实参按标量/指针逐个传递（struct 值经宿主 ABI 无法传递，非法）；
  可变部分实参须为标量/指针。
- **签名提示**：NanoC 类型系统无 `const`，宿主侧 `const char*` 形参（如 `puts`）
  与 NanoC 的 `char*` 声明在 C/LLVM 原生链接时可能触发宿主编译器限定符**警告**
  （行为不受影响）；VM 链路经签名包装器适配，无此警告。

**三条后端的链接差异（同一份源码，链接期行为不同）**：

| 后端 | 外部符号解析 |
|---|---|
| NAS/VM | 汇编产物携带 `extern 符号` 指令（可变参数符号按 cdecl 约定标注）；`nvm --host-lib <dll>` 加载宿主动态库，**按白名单**（§7.3）经签名包装器解析；白名单外的符号明确报错 |
| C | 发出带签名的 C 原型，原生链接完整 libc（任意 C 函数可用，前提宿主有头文件签名匹配的声明） |
| LLVM | 发出 `declare`，原生链接完整 libc |

即：`puts` 在三后端都可用，但 VM 链路必须 `--host-lib`；VM 上**只有白名单函数**可用。

### 7.2 `#include "x.h"`（声明子集，R9）

`#include "rel/path.h"` 引入 C 头文件的**声明子集**（仅引号形式、目标必须 `.h`、
相对当前文件目录；`<...>` 系统头不支持）：

- **识别并接受**：`#ifndef G/#define G/.../#endif` 的 include guard（剥离这三行）；
  `#pragma once`；`#define NAME 值` 对象宏（文本替换、单层展开、定义点之后生效；
  无值宏替换为空；NAME 为 NanoC 关键字时该行忽略）；`enum [Tag] { ... }` 定义
  （原位改写为 `int`，枚举器按整型常量登记）；C 限定符/修饰符
  `const/volatile/static/inline/register`（语义忽略）与
  `unsigned/signed/long/short`（**按宿主基础类型解释为 int**——VM 字宽 32 位裁定）；
  函数原型（`int add(int a, int b);`，无名形参合法，数组形参 `[...]` 按 C 语义
  退化一级指针）；`(void)` 空参表；原型与定义合并（C 原型语义，签名兼容幂等）。
- **预置类型表**：`int8_t/int16_t/int32_t/uint8_t/.../uint64_t/size_t/ptrdiff_t/
  intptr_t/bool/_Bool` 预置为 `int`（64 位类型折叠为 int——VM 字宽裁定）；
  `true`/`false` 预置为 `1`/`0`。
- **识别并报错（宁报错不误编译）**：`#undef`/`#error`/`#line`/空指令/未知指令/
  `#define` 续行/函数宏/其余条件编译（`#if`/`#ifdef`/`#elif`/`#else`/非 guard 的
  `#endif`）/其余 `#pragma`/`float`/`double` 类型。
- 头文件中的声明为**全编译单元可见**（等价 C 翻译单元语义）。

### 7.3 VM 宿主白名单（`--host-lib`）

`nvm --host-lib <path>`（可重复，按序解析）按符号名解析动态导入。**已知签名
白名单**（白名单外的符号报错，避免错误 ABI 调用）：

| 符号 | C 签名 | 备注 |
|---|---|---|
| `puts` | `int puts(const char*)` | 实参必须是 VM 统一内存中的有效 NUL 终止串，越界/未终止报运行期错误 |
| `putchar` | `int putchar(int)` | |
| `abs` | `int abs(int)` | |
| `atoi` | `int atoi(const char*)` | 同 `puts` 的字符串安全检查 |
| `strlen` | `size_t strlen(const char*)` | 返回值截断为 int32 |
| `exit` | `void exit(int)` | 以实参码终止 nvm 进程 |
| `GetTickCount` | `DWORD GetTickCount(void)` | 仅 Windows；旧格式动态链接路径回归用 |

### 7.4 import/export 与 C 互操作的分工

- NanoC 程序间复用：`import`/`export`（`.nc` 文件即模块）。
- 与 C 函数库互操作：`extern` 声明（内联签名）或 `#include "x.h"`（声明子集）。
- 两类机制可同程序混用；头文件先于包含者解析，其 typedef 名注入后续文件。

---

## 8 可观测行为与工具链契约

### 8.1 编译器契约（ncc）

```
ncc [options] <file.nc>...
  -o <file>          输出路径（默认：<首个输入>.nas）
  --emit=<kind>      asm（默认）| c | llvm | obj | exe
  -MMD / -MF <file>  依赖文件（含全部 import 闭包）
  --dump-ir          将降级后的 ir::Module 转储到 stderr
  --version          版本（ncc 0.1.0）
```

- 多个输入文件**合并为一个编译单元**（命令行顺序即装载根顺序）。
- 诊断一律 `file:line:col: error|warning: message` 输出到 stderr；存在 error 时
  退出码非 0 且不生成产物。
- `--emit=llvm|obj|exe` 需要 LLVM 工具链（探测顺序：环境变量 `NANOC_LLVM_DIR` >
  PATH 上的 `llc`）；工具链缺失时明确报错。

### 8.2 汇编/链接/运行契约

```
nas <input.nas> <output.nci>        # 汇编为 NCI v2.1 目标
nas -r a.nci b.nci -o app.nci       # 链接 NCI 目标（段合并/重定位/符号解析）
nvm app.nci [--host-lib <dll>...] [--Xss=<size>{b,k,m}]
```

- VM 默认栈 8 MiB（`--Xss` 可调）；数据段与栈统一编址。
- NCI v2.1 文件布局/导入导出表/调用约定的权威定义见
  《Bytecode Format Specification v2.1》，本规范不重复。

### 8.3 退出码（钉死）

- **VM**：`main` 返回值写入 R0（int32）；**nvm 进程退出码 = R0 & 0xFF**
  （本行为由并行 PR 落地中；落地区间内 nvm 恒返 0，属实现滞后，见 §11.1 不一致清单 I1）。
- **C/LLVM 原生**：进程退出码 = `main` 返回值（32 位全宽）。POSIX 宿主按惯例截断
  到低 8 位；Windows 保留全宽（`return -30` 经 cmd 可观察到 -30）。
- **比较口径（裁定）**：与 VM R0 的差分比较统一按低 8 位归一化
  （`R0 & 0xFF` 对 `原生退出码 & 0xFF`）。程序**应当**把 `main` 返回值保持在
  `[0, 255]`，使映射保持单射（255 截断边界：R0 = 256 与 R0 = 0 在该口径下同余不可区分）。
- `exit(n)`（经宿主白名单）以 n 终止进程（同一比较口径，三后端一致）。

### 8.4 三后端差分一致性保证

同一程序在三后端上**可观测行为一致**，映射口径：

```
VM:   R0（int32）      →  exitCode8 = R0 & 0xFF
原生：进程退出码（int32）→  exitCode8 = exit & 0xFF
```

- 标准输出经宿主 I/O（puts/putchar）时，三后端字节流一致。
- 一致性的**前提**是程序不依赖 §9 清单中的 UB（有符号溢出、越界、除零等在三后端
  表现不同）。**约定**：差分矩阵中的程序将 `main` 返回值保持在 `[0, 255]`，
  使 R0 → exit 的映射保持单射（255 截断边界：R0=256 与 R0=0 在低 8 位口径下同余）。

---

## 9 未定义行为与实现定义行为清单

教学语言原则：**能裁定的不留 UB**。以下为全部 UB 与实现定义点。

### 9.1 未初始化局部变量

**UB**。局部变量（标量/指针/struct/数组）声明时未初始化，其值不确定；实现不为
局部变量清零（VM 栈槽保留残留值；原生栈对象同）。**对比**：未初始化全局变量
恒为 0（裁定，§5.2）。程序必须在读取前初始化局部变量。

### 9.2 有符号整数溢出

**UB**。`+ - *` 溢出 32 位补码范围时：VM 按补码回绕；C 后端触发 C UB；
LLVM 后端为 poison。三后端在溢出程序上**不保证一致**。

### 9.3 除零 / 模零

**UB**。`x / 0`、`x % 0`：VM 上除法指令静默跳过且不推进 PC（表现为挂死——实现
现状，按 UB 上限归类）；C 后端为硬件异常；LLVM 后端为 UB。

### 9.4 非 ASCII 字符字面量

**实现定义**。字节值 128..255 的字符字面量：LLVM 后端按 `i8` 有符号解释（-128..-1），
match 模式中的字符常量按无符号码点（128..255）参与匹配。**程序应当只使用 ASCII
字符字面量**。

### 9.5 修改字符串字面量

**UB（裁定上限）**。字符串字面量对象为只读常量：VM 数据段在统一内存中（现状
可写，写入"成功"但行为不作保证）；C 后端发射 `static const char[]`（写入崩溃或
UB）。程序不得写串。

### 9.6 解引用无效指针

**UB**。解引用 `NULL`、未初始化指针、悬垂指针：VM 上统一内存寻址可能"成功"读到
栈数据（无 MMU），原生后端为段错误。语言无任何指针校验。

### 9.7 数组越界

**UB**。`a[i]` 越界与指针越界解引用无任何边界检查（三后端一致地不检查）。

### 9.8 其他 UB 与实现定义点

| 点 | 分类 | 现状 |
|---|---|---|
| coro 实例槽耗尽（>16/函数）后 `coro_create` | UB | bump 计数越界读写帧数组越界 |
| 栈溢出（深递归） | UB | VM 默认 8 MiB 栈（`--Xss` 可调）；原生为宿主线程栈 |
| 实参/操作数求值顺序 | 实现定义 | 三后端当前均按源顺序求值；程序不得依赖 |
| VM 字宽下 struct 布局 vs 原生自然布局 | 实现定义 | §4.3；成员访问各自正确，字节级布局不得跨后端依赖 |
| `char` 在 VM 上的存储宽度（一字） vs 原生 1 字节 | 实现定义 | §4.2；`char*` 不可解引用/下标，规避可见性 |
| 诊断列号按字节计 | 实现定义 | §2.1 |

---

## 10 版本与演进

### 10.1 规范版本

- **NanoC Language Specification v1.0**：对 main@7cc576f 实现的完整快照（本文）。
- 规范与实现同步演进的纪律：语言级改动**先改本文**、同步三后端与 nvm 实现及测试，
  再合入（与 NCI v2.1 字节码规范同纪律）。

### 10.2 已裁定决策表

| # | 决策 | 结论 |
|---|---|---|
| D1 | 整型宽度 | `int` = 32 位补码（i32）；无浮点、无 unsigned（教学裁定） |
| D2 | 退出码映射 | 退出码统一 `& 0xFF`：nvm = R0 & 0xFF（并行 PR 落地中）、原生 = 平台惯例 |
| D3 | match 区间 | 闭区间（含端点）；未穷尽 = 警告；未命中 = 0；块分支值 = 0 |
| D4 | defer 捕获 | Go 语义：注册点求值捕获，退出点逆序执行 |
| D5 | 协程模型 | 无栈协程 = IR 状态机（A2）；返回类型限 int；16 实例/函数、不回收（一期） |
| D6 | yield × defer | 硬约束互斥（编译期拒绝） |
| D7 | 窄化转换 | int→char 无隐式窄化、无强制转换语法（宁可让程序员写显式路径） |
| D8 | struct 布局 | VM：4 字节对齐无填充；原生：自然布局 + 分歧说明（§4.3） |
| D9 | 全局零初始化 | 未初始化全局 = 0；未初始化局部 = UB（实现不清零） |
| D10 | 类型命名空间 | struct/typedef 全编译单元可见，不强制文本先序 |
| D11 | 定宽类型路线（R15 规划） | 引入 Rust 式定宽类型：`i32` 为规范类型名、`int` 为兼容别名（并行 PR 落地中）；`i8/i16/i64/u*/bool` 为 R15 规划项 |

### 10.3 演进边界（Out of Scope，摘自 PRD §10）

宏系统、编译器优化 pass、GC、C++ 特性、运行时动态加载/反射、线程、完整 libc
移植、多维数组、位域、union、goto、变长数组、IDE/LSP——均不在语言层演进范围。

---

## 11 附录

### 11.1 文档-实现不一致清单（以实现为现状记录，待后续 PR 修复）

| # | 条目 | 规范目标 | 实现现状 |
|---|---|---|---|
| I1 | nvm 退出码 | `R0 & 0xFF`（§8.3） | `nvm/src/nvm/main.cpp` 当前恒 `return 0`；并行 PR 落地中 |
| I2 | 定宽类型名 | `i32` 为规范类型名、`int` 为别名（D11） | 当前仅有 `int`；并行 PR 落地中 |
| I3 | 字符字面量转义 | §2.6 不支持转义（`'\n'` 为词法错误） | 实现一致；仅提示：这是与 C 的差异点，若后续放开须同步三后端与本文 |
| I4 | 求值顺序 | 未指定（§5.4） | 三后端当前均按源顺序；如引入优化须更新 §9.8 口径 |
| I5 | `_` 保留拼写 | §2.3 规定程序不得用 `_` 作变量名 | 实现不在编译期诊断（`int _ = 5;` 可编译）；诊断属后续增强 |
| I6 | 原生退出码宽度 | §8.3 Windows 全宽、POSIX 低 8 位、比较按 `& 0xFF` 口径 | 实现一致；记录以正音：差分矩阵文档中 "C 退出码按 & 0xFF 比较" 指归一化口径而非 OS 行为 |
| I7 | 整型字面量溢出诊断 | §2.5 编译期错误（行为一致） | 错误消息透出内部细节 `stoi argument out of range`，宜改为面向用户的措辞 |

### 11.2 示例真机验证清单

本文全部示例代码取自以下验证集（`vm` = ncc→nas→nvm 链路；`exe` = ncc --emit=exe
LLVM 链路；`c` = ncc --emit=c → clang 编译运行链路；同一示例三链路退出码/输出一致，
原生退出码按 §8.3 低 8 位口径比较）：

| 示例 | 覆盖特性 | 验证链路 | 观测 |
|---|---|---|---|
| 最小程序（§1.3） | 函数/return | vm / c / exe | 退出码 0 |
| puts 问候（§2.6） | extern + 字符串 + 宿主链接差异 | vm(--host-lib msvcrt.dll) / c / exe | 三链路输出 `hello`（c 链路有宿主 const 限定符警告，§7.1） |
| 窄化负例（§4.6） | 隐式转换禁令 | 编译错误 | `cannot implicitly convert int to char in assignment to 'c'` |
| import/export 两文件（§5.7） | 多文件编译单元 | vm / c / exe | 退出码 3 |
| defer 值捕获（§6.1） | defer 注册时求值 | vm / c / exe | 退出码 99 |
| match 五类模式（§6.2） | 常量/区间/多值/守卫/通配 | vm / c / exe | 退出码 60 |
| coro 迭代器（§6.3） | coro/yield 状态机 | vm / c / exe | 退出码 30 |
| struct/指针/数组/NULL/`for(;;)`（§4/§5） | 聚合与指针核心 | vm / c / exe | 退出码 30 |
| 递归 fib(10)（§5.5） | 递归/前向引用 | vm / c / exe | 退出码 0 |
| `return 300`（§8.3） | 退出码截断口径 | c / exe | bash 观测 44（= 300 & 0xFF）；Windows 全宽为 300 |

验证环境：Windows x64，MSVC 2022 构建，clang+llvm 18.1.8（`NANOC_LLVM_DIR`），
msvcrt.dll 宿主库。

### 11.3 参考文档

- 《Bytecode Format Specification v2.1》——NCI 目标格式与 VM 执行模型权威规范
- 《PRD-多后端编译与语言特性》——需求路线图与决策记录（A1-A11）
- 《Complier Design Description》——工具链流水线总览
- 实现源码：`ncc/src/ncc/`（lexer/parser/ast/semantic/ir/codegen/c_backend/
  llvm_backend/preprocessor/loader）、`nvm/src/nvm/`、`nas/src/nas/`

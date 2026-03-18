# NanoC 编译器示例程序

本文档展示了NanoC编译器能够编译的各种C语言程序示例。

## 示例程序列表

### 1. hello.nc - 基本变量和算术运算

```c
int main() {
    int a = 10;
    int b = 20;
    int c = a + b;
    return c;
}
```

**生成的汇编代码特点：**
- 变量声明和初始化
- 加法运算
- 函数序言和尾声

---

### 2. arithmetic.nc - 多种算术运算

```c
int main() {
    int x = 10;
    int y = 3;
    int sum = x + y;
    int diff = x - y;
    int prod = x * y;
    return sum;
}
```

**生成的汇编代码特点：**
- 加法、减法、乘法运算
- 多个变量操作

---

### 3. control_flow.nc - 条件语句

```c
int main() {
    int x = 10;
    int result = 0;
    
    if (x > 5) {
        result = 1;
    } else {
        result = 0;
    }
    
    return result;
}
```

**生成的汇编代码特点：**
- 比较运算 (`gt`)
- 条件跳转 (`jic`)
- 无条件跳转 (`jmp`)
- 标签生成

---

### 4. functions.nc - 函数定义和调用

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(10, 20);
    return result;
}
```

**生成的汇编代码特点：**
- 函数定义
- 参数传递
- 函数调用 (`call`)
- 返回值处理

---

### 5. loop.nc - while循环

```c
int main() {
    int sum = 0;
    int i = 1;
    while (i <= 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

**生成的汇编代码特点：**
- 循环结构
- 比较运算 (`le`)
- 条件跳转
- 变量自增

---

## 编译器支持的C语言特性

### 数据类型
- ✅ `int` - 整数类型

### 变量操作
- ✅ 变量声明
- ✅ 变量初始化
- ✅ 变量赋值

### 运算符
- ✅ 算术运算：`+`, `-`, `*`, `/`, `%`
- ✅ 比较运算：`==`, `!=`, `<`, `<=`, `>`, `>=`
- ✅ 逻辑运算：`&&`, `||`, `!`

### 控制流
- ✅ `if-else` 条件语句
- ✅ `while` 循环
- ✅ `for` 循环

### 函数
- ✅ 函数定义
- ✅ 函数调用
- ✅ 参数传递
- ✅ 返回值

### 其他
- ✅ 注释（单行和多行）
- ✅ 复合语句（代码块）

---

## 运行示例

```bash
# 编译示例程序
xmake build compile_examples

# 运行编译器
xmake run compile_examples
```

## 输出格式

编译器生成NanoC汇编代码（.nas文件），包含：
- 函数标签
- 栈帧管理（push BP, lmm BP, SP）
- 变量访问（通过BP偏移）
- 表达式求值
- 控制流指令

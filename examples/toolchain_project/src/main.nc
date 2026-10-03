// main 模块：入口。import math 后按签名调用（独立编译下合成 extern 原型，
// C 链接器解析到 math.obj 里的定义）。
// main 返回 square(add(2, 5)) = 7*7 = 49（退出码承载结果，口径同 hello 样例）。

import math;

int main() {
    return square(add(2, 5));
}

// NanoC 样例：纯计算 + 返回值，无外部依赖（不依赖 R3 extern / C 库函数）。
// 期望：程序以退出码 42 结束（add(40, 2) 的结果）。

int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 40;
    int y = 2;
    int sum = add(x, y);
    return sum; // 42
}

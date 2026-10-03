// 协程迭代器（PRD R12）：coro/yield 生成平方数序列。
// coro 函数经编译器状态机变换降解；yield 挂起产出值，resume 恢复续跑。
coro int squares(int n) {
    for (int i = 0; i < n; i = i + 1) {
        yield i * i;
    }
    return -1;
}

int main() {
    int h = coro_create(squares, 5);
    int sum = 0;
    int v = coro_resume(h);
    while (v >= 0) {
        sum = sum + v;
        v = coro_resume(h);
    }
    return sum; // 0+1+4+9+16 = 30
}

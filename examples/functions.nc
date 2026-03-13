// 函数调用示例
int add(int a, int b) {
    return a + b;
}

int multiply(int x, int y) {
    return x * y;
}

int main() {
    int result1 = add(10, 20);
    int result2 = multiply(5, 6);
    int final = add(result1, result2);
    return final;
}

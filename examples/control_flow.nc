// 控制流测试
int main() {
    int x = 10;
    int result = 0;
    
    if (x > 5) {
        result = 1;
    } else {
        result = 0;
    }
    
    int i = 0;
    while (i < 5) {
        result = result + i;
        i = i + 1;
    }
    
    return result;
}

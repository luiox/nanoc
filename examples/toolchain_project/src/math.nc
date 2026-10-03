// math 模块：导出两个纯函数（PRD R2a 文件即模块，export 控制跨模块可见性）
// 独立编译（PRD R7）：本模块单独产出 .c/.obj，导入方只看到本模块的导出签名
// （extern 原型），函数体留在本模块的 obj 里，由 C 链接器在目标级解析。

export int add(int a, int b) {
    return a + b;
}

export int square(int x) {
    return x * x;
}

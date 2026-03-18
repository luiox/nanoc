; 简单的算术运算测试
; 计算 10 + 20 并返回结果

main:
    lmm R0, 10      ; 加载10到R0
    lmm R1, 20      ; 加载20到R1
    push R1         ; 保存R1的值
    pop R0          ; 恢复到R0
    add R0, 10      ; R0 = R0 + 10
    trap 2          ; HALT

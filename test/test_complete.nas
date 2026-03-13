; 完整的工具链测试
; 测试所有主要功能

main:
    ; 测试算术运算
    lmm R0, 100      ; R0 = 100
    lmm R1, 50       ; R1 = 50
    add R0, 50       ; R0 = 150
    sub R0, 25       ; R0 = 125
    mul R0, 2        ; R0 = 250
    div R0, 5        ; R0 = 50
    
    ; 测试栈操作
    push R0          ; 保存R0
    lmm R0, 0        ; 清空R0
    pop R0           ; 恢复R0
    
    ; 测试比较指令
    lmm R1, 50       ; R1 = 50
    eq R0, R1        ; 比较R0和R1
    jic equal        ; 如果相等则跳转
    
    ; 不相等的情况
    lmm R0, 0
    jmp end
    
equal:
    lmm R0, 1
    
end:
    trap 2           ; HALT

    lmm r0, 0 ; r0 = 0
    lmm r1, 1 ; r1 = 1
    lmm r2, 10 ; r2 = 10
LOOP_FLAG:
    add r0, r1 ; r0 += r1
    add r1, 1 ; r1 += 1
    sub r2, 1 ; r2 -= 1
    eq r2, 0 ; r2 == 0
    jic LOOP_FLAG ; if r2 == 0 goto LOOP_FLAG
    trap 0 ; exit

nvm为被设计为32位栈式虚拟机
默认情况下是8MB的栈空间

指令集目前有下面这些内容，一共27条指令。

寄存器和内存之间的转移指令：lmm、st
加载一个内存指令：lea
算术运算指令：add、sub、mul、div、mod、not、and、or、xor、shl、shr
比较指令：eq、ne、lt、le、gt、ge
栈操作指令：push、pop
跳转指令：jmp、jic
函数调用指令：call、ret
一些内建指令：trap

指令长度为


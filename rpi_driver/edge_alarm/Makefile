# 定义你要生成的模块名（注意：这里的 .o 最终会被内核构建系统转化为 .ko）
obj-m += edge_alarm.o

# 获取当前树莓派正在运行的内核源码/头文件路径
KDIR := /lib/modules/$(shell uname -r)/build

# 获取当前工作目录
PWD := $(shell pwd)

# 默认目标：调用内核的 Makefile 来编译我们当前目录下的代码
all:
	make -C $(KDIR) M=$(PWD) modules

# 清理目标：删除编译产生的临时文件和 .ko 文件
clean:
	make -C $(KDIR) M=$(PWD) clean
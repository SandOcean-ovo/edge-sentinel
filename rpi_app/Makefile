# 编译器和编译选项
CC = gcc
CFLAGS = -Wall -g -Iinclude

# 目录规划 (不要写死在规则里，用变量管理)
SRC_DIR = src
BUILD_DIR = build

# 最终目标程序的名字
TARGET = $(BUILD_DIR)/edge_gatewayd

# 1. 自动获取所有的 .c 文件 
SRCS = $(wildcard $(SRC_DIR)/*.c)

# 2. 神奇的替换函数：把 src/xxx.c 替换成 build/xxx.o 
# 这样你的 OBJS 就变成了 build/main.o build/conf.o，而不是 src 里的了！
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# --- 规则部分 ---

# 默认目标：先创建 build 目录，再生成可执行文件
all: $(BUILD_DIR) $(TARGET)

# 创建 build 目录 (-p 保证目录已存在时不会报错)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 链接规则：依赖所有的 build/xxx.o，生成最终的 target
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)
	@echo "Build success! Run with: ./$(TARGET)"

# 核心魔法：编译规则
# 意思是：要生成 build/xxx.o，请寻找对应的 src/xxx.c
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则：直接把整个 build 文件夹干掉就行了，清清爽爽
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
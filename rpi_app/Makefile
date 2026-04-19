# 编译器和编译选项
CC = gcc
CFLAGS = -Wall -O2 -g -Iinclude
LIBS = -lsqlite3
# 如果你的测试文件在 tests 目录，需要加上这个包含路径
CFLAGS += -I. 
CFLAGS += -Iexternal

# 目录规划
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = tests
EXTERNAL_DIR = external

# 最终目标程序和测试程序的名字
TARGET = $(BUILD_DIR)/edge_gatewayd
TEST_TARGET = $(BUILD_DIR)/test_suite

# 1. 获取所有的源码文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
# 2. 确定哪个是应用的主入口 (main.c)
APP_MAIN_SRC = $(SRC_DIR)/main.c
# 3. 提取出所有的“库逻辑”文件（即排除掉 main.c 的剩余文件）
LIB_SRCS = $(filter-out $(APP_MAIN_SRC), $(SRCS))
EXTERNAL_SRCS = $(wildcard $(EXTERNAL_DIR)/*.c)
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(LIB_SRCS))
EXTERNAL_OBJS = $(patsubst $(EXTERNAL_DIR)/%.c, $(BUILD_DIR)/%.o, $(EXTERNAL_SRCS))

# 4. 获取测试文件
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.o, $(TEST_SRCS))

# --- 规则部分 ---

all: $(BUILD_DIR) $(TARGET)

# 生成应用主程序：链接 库逻辑 + 外部库 + 应用主入口
$(TARGET): $(LIB_OBJS) $(EXTERNAL_OBJS) $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(APP_MAIN_SRC))
	$(CC) $^ -o $(TARGET) $(LIBS)
	@echo "Build app success!"

# 生成测试程序：链接 库逻辑 + 外部库 + 测试主入口
test: $(BUILD_DIR) $(TEST_TARGET)
	@echo "Running tests..."
	./$(TEST_TARGET)

$(TEST_TARGET): $(LIB_OBJS) $(EXTERNAL_OBJS) $(TEST_OBJS)
	$(CC) $^ -o $(TEST_TARGET) $(LIBS)
	@echo "Build test success!"

# 创建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译 src 目录下的 .c
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译 external 目录下的 .c
$(BUILD_DIR)/%.o: $(EXTERNAL_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译 tests 目录下的 .c
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean test
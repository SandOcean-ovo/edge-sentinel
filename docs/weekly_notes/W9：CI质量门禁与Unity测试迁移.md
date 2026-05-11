# W9：CI 质量门禁与 Unity 测试迁移

## 本周目标

第九周的目标是把前几周积累的用户态功能纳入自动化质量检查：引入 Unity 单元测试框架，补齐协议解析器等关键模块测试，并配置 GitHub Actions 流水线，让每次 push / pull request 都能自动完成单元测试、测试报告生成、ARM 交叉编译检查和静态分析。

最终目标不是单纯“能跑测试”，而是形成一个可重复的质量门禁：

- 本地可以用 `make test` 快速验证功能正确性。
- CI 可以用 `make test-xml` 生成 JUnit XML 测试报告。
- CI 可以用 `make compile-check CC=arm-linux-gnueabihf-gcc` 验证树莓派 ARM 架构编译。
- CI 可以用 `make cppcheck` 执行静态分析，并让 warning / style / performance 问题阻塞合并。

## 为什么迁移到 Unity

原来的测试方式主要依赖裸 `assert()`，适合早期快速验证，但不适合 CI 报告和持续维护。

| 对比项 | 裸 `assert()` | Unity |
|--------|---------------|-------|
| 失败行为 | 第一个失败直接 abort，后续测试全跳过 | 记录失败，继续执行剩余测试 |
| 输出格式 | 无统一统计，只有崩溃或人工打印 | 输出 `14 Tests 0 Failures 0 Ignored`，方便 CI 解析 |
| 浮点比较 | 需要手写 epsilon 判断 | `TEST_ASSERT_FLOAT_WITHIN` 内置支持 |
| 测试组织 | 函数之间缺少统一 runner | `UNITY_BEGIN` / `RUN_TEST` / `UNITY_END` 统一调度 |
| CI 友好度 | 很难生成标准测试报告 | 可转换为 JUnit XML artifact |

本周将 Unity 源码放入 `rpi_app/external/`，并在 `.gitignore` 中保留该目录的版本跟踪。Unity 作为测试框架源码，需要随仓库一起提交，保证 CI 环境不需要额外下载。

## 测试覆盖

### 已迁移到 Unity 的测试

| 测试函数 | 覆盖模块 | 验证点 |
|----------|----------|--------|
| `test_crc16_known_value` | CRC16 | 固定报文校验值是否稳定 |
| `test_ringbuf_basic_ops` | RingBuf | 写入、读取、满缓冲、环绕读写 |
| `test_config_thresholds` | 配置解析 | 波特率、阈值、设备 ID、路径、IP、端口 |
| `test_config_file_missing` | 配置解析 | 配置文件不存在时返回错误 |
| `test_gateway_pose_snapshot` | 姿态读取 | 模拟 IIO sysfs，验证 raw 数据、g 值和姿态角 |
| `test_gateway_pose_check_threshold` | 用户态告警兜底 | peak / rms / gyro 阈值触发与关闭 |
| `test_ioctl_commands` | ioctl 共享头 | ioctl 命令码唯一性、魔数、设备节点 |
| `test_alarm_pose_json` | 协议编码 | 告警姿态 JSON 字段和值 |

### 新增协议解析器测试

协议解析器是串口输入路径的关键模块。本周新增了 6 个测试覆盖常见串口边界场景。

| 测试函数 | 场景 | 验证点 |
|----------|------|--------|
| `test_parse_valid_heartbeat` | 正常心跳帧 | 解析成功，`type = 0x01`，缓冲区清空 |
| `test_parse_half_packet` | 半包 | 数据不完整时返回 false，补齐后解析成功 |
| `test_parse_sticky_packets` | 粘包 | 两帧连续写入后可连续解析两次 |
| `test_parse_crc_error` | CRC 错帧 | 校验失败时拒绝该帧 |
| `test_parse_garbage_then_valid` | 垃圾数据 + 有效帧 | 跳过垃圾字节，找到有效包头 |
| `test_parse_empty_buffer` | 空缓冲区 | 返回 false，不越界、不崩溃 |

测试中使用 `build_frame()` 构造标准协议帧：

```text
[0xAA] [0x55] [len] [type] [payload...] [CRC16-L] [CRC16-H]
```

其中 CRC16 覆盖从包头到 payload 末尾，低字节在前。

## Makefile 调整

本周将 `Makefile` 从单纯构建测试程序，扩展成了本地和 CI 共用的入口。

| Target | 用途 |
|--------|------|
| `make all` | 编译正式用户态程序 `build/edge_gatewayd` |
| `make test` | 编译并运行 Unity 单元测试 |
| `make test-xml` | 运行测试并生成 `build/test_report.xml` |
| `make compile-check` | 只编译对象文件，用于 ARM 交叉编译检查 |
| `make cppcheck` | 执行 rpi_app 用户态源码静态分析 |

### 应用程序和测试程序分离链接

修复前，正式程序链接时会把 `external/unity.c` 也链接进去，导致 `make all` 出现 `setUp` / `tearDown` 未定义的问题。原因是应用程序不应该依赖 Unity，Unity 只属于测试目标。

本周将外部源码拆成两组：

```make
APP_EXTERNAL_SRCS = $(EXTERNAL_DIR)/cJSON.c
TEST_EXTERNAL_SRCS = $(wildcard $(EXTERNAL_DIR)/*.c)
```

这样正式程序只链接 `cJSON.c`，测试程序链接 `cJSON.c + unity.c`，职责更清楚。

## GitHub Actions 流水线

本周新增并验证了三类 CI job。

### Unit Tests

单元测试 job 负责安装依赖、编译并运行测试，然后生成 JUnit XML 报告。

```yaml
- name: Build and run tests
  working-directory: rpi_app
  run: make test

- name: Generate JUnit XML report
  working-directory: rpi_app
  run: make test-xml
```

`make test-xml` 会将 Unity 输出保存到：

```text
rpi_app/build/test_stdout.txt
```

再通过 `awk` 解析 `PASS` / `FAIL` 行，生成：

```text
rpi_app/build/test_report.xml
```

该报告由 `actions/upload-artifact` 上传，便于在 GitHub Actions 页面下载查看。

### ARM Cross-Compile

交叉编译 job 安装 `gcc-arm-linux-gnueabihf`，执行：

```bash
make clean
make compile-check CC=arm-linux-gnueabihf-gcc
```

由于 CI 环境不一定具备 ARM 版 `libsqlite3`，`compile-check` 只编译对象文件，不强制链接。这样可以验证大部分 ARM 编译兼容问题，同时避免依赖安装过重。

流水线中还保留了一个 best-effort full link，尝试安装 `libsqlite3-dev:armhf` 后完整链接。如果失败不阻塞主流程。

### Static Analysis

静态分析 job 安装 `cppcheck`，然后在 `rpi_app` 下执行：

```bash
make cppcheck
```

`make cppcheck` 使用统一参数：

```bash
cppcheck --enable=warning,style,performance \
         --error-exitcode=1 \
         --suppress=missingIncludeSystem \
         --suppress=toomanyconfigs \
         --suppress=normalCheckLevelMaxBranches \
         -URUN_TEST \
         -Iinclude -Iexternal \
         src/ tests/
```

这样做的意义是：本地和 CI 使用同一条命令，避免出现“本地通过、CI 参数不同又失败”的情况。

## 驱动自动化测试脚本

本周还整理了 `scripts/driver_test.sh`，用于在树莓派上自动化验证两个内核驱动的基本链路。

脚本定位是“硬件在环冒烟测试”，覆盖从构建到加载、`edge_alarm` 字符设备检查、`gateway_monitor` IIO 数据读取、内核日志检查和卸载清理的一条完整路径：

```text
构建 edge_alarm.ko
  -> 构建 gateway_monitor.ko
  -> root / 树莓派平台检查
  -> insmod edge_alarm.ko
  -> insmod gateway_monitor.ko
  -> 检查 /dev/edge_alarm
  -> 读取 MPU6050 IIO sysfs 原始值
  -> 保存并筛查 dmesg
  -> rmmod 清理模块和设备节点
```

脚本支持两种模式：

| 模式 | 命令 | 用途 |
|------|------|------|
| 快速构建检查 | `sudo ./scripts/driver_test.sh --quick` | 只编译驱动，不执行 `insmod` / `rmmod` |
| 完整硬件测试 | `sudo ./scripts/driver_test.sh` | 在树莓派上加载驱动并验证设备节点、IIO 数据和 dmesg |

完整模式要求：

- 在树莓派上运行。
- 使用 root 权限运行。
- 当前内核已安装可用的 `/lib/modules/$(uname -r)/build`。
- `edge_alarm` 所需设备树 overlay 已加载。
- MPU6050 硬件已连接，并能被 `gateway_monitor` 绑定为 IIO 设备。

本周检查脚本时发现一个 dmesg 统计 bug：

```bash
ERROR_COUNT=$(grep -ci "error\|oops\|bug\|warning" "$DMESG_OUT" 2>/dev/null || echo "0")
```

`grep -c` 在无匹配时会输出 `0` 但返回非 0，后面的 `echo "0"` 会再输出一次，导致 `ERROR_COUNT` 变成两行 `0`，后续 `[[ "$ERROR_COUNT" -eq 0 ]]` 可能触发数值比较异常。

修复为：

```bash
ERROR_COUNT=$(grep -Eci "error|oops|bug|warning" "$DMESG_OUT" 2>/dev/null || true)
```

这样无匹配时只保留一个 `0`，既避免脚本因 `set -euo pipefail` 意外退出，也让日志判断逻辑保持稳定。

随后又发现一个 sudo 场景下的日志文件权限问题。脚本原先使用固定路径：

```bash
LOG_FILE="/tmp/edge_driver_test.log"
```

如果先用普通用户运行脚本，这个文件会由普通用户创建；再用 `sudo` 运行时，root 进程在 `/tmp` 这种 sticky 目录里用 `tee -a` 追加该用户文件，可能被 Linux 的 `fs.protected_regular` 保护策略拒绝，表现为：

```text
tee: /tmp/edge_driver_test.log: Permission denied
```

修复为每次运行创建独立日志文件：

```bash
LOG_FILE="$(mktemp "${TMPDIR:-/tmp}/edge_driver_test.XXXXXX.log")"
```

脚本会在环境检查阶段打印本次日志路径，避免普通用户运行和 sudo 运行争用同一个 `/tmp` 固定文件。

另外，平台检测也做了加固。原脚本只检查：

```bash
grep -q "BCM\|bcm\|raspberry" /proc/cpuinfo
```

在 Raspberry Pi 5 / 64 位系统上，`/proc/cpuinfo` 可能显示为：

```text
Model        : Raspberry Pi 5 Model B Rev 1.1
```

原匹配是大小写敏感的，小写 `raspberry` 无法匹配大写 `Raspberry`，导致脚本误判为“非树莓派平台”。修复后脚本优先读取更可靠的设备树模型：

```bash
/proc/device-tree/model
```

并使用大小写不敏感匹配；如果设备树路径不可用，再回退检查 `/proc/cpuinfo` 中的 `raspberry pi` 或 `bcm*` 标识。

## cppcheck 失败问题与修复

本周 push 时遇到的主要阻塞来自 `cppcheck --error-exitcode=1`。在该模式下，warning / style / performance 级别的问题都会让 CI 失败。

### 1. const 修饰缺失

cppcheck 报告了多处可以声明为 const 的变量或参数：

| 文件 | 问题 | 修复 |
|------|------|------|
| `src/gateway_pose.c` | `struct dirent *ent` 可为 const pointer | 改为 `const struct dirent *ent` |
| `src/log.c` | `level_str` / `color_code` 指向字符串字面量 | 改为 `const char *` |
| `src/ringbuf.c` / `include/ringbuf.h` | `RingBuf_writeblocks` 不修改输入数据 | `uint8_t *psrc` 改为 `const uint8_t *psrc` |
| `src/ringbuf.c` / `include/ringbuf.h` | `RingBuf_getreadable` 不修改 ringbuf | `RingBuf_t *pbuf` 改为 `const RingBuf_t *pbuf` |
| `src/crc16.c` / `include/crc16.h` | CRC 计算不修改输入报文 | `uint8_t *pdata` 改为 `const uint8_t *pdata` |
| `tests/test_main.c` | 测试输入数组不被修改 | `sample_msg` / `data_to_wrap` 改为 const 数组 |

这些修改不改变运行逻辑，但能让接口表达更准确，也能防止调用者误修改输入缓冲区。

### 2. Unity 断言宏导致空指针误报

cppcheck 不完全理解 Unity 的断言宏控制流。例如：

```c
TEST_ASSERT_NOT_NULL(root);
make_path(path, sizeof(path), root, "test.conf");
```

从测试语义看，如果 `root == NULL`，Unity 会标记失败并中止当前测试；但静态分析器可能认为后续代码仍会继续执行，于是报 `nullPointerRedundantCheck` 或 `ctunullpointer`。

本周将相关 helper 改成显式保护分支：

```c
if (root == NULL) {
    TEST_FAIL_MESSAGE("mkdtemp failed");
    return;
}
```

同类调整还包括：

- `make_path()` 对 `out` / `dir` / `name` 做显式 NULL 检查。
- `write_text_file()` 对 `fopen` 失败做显式返回。
- `require_object()` / `require_number()` 在 JSON 字段缺失时显式失败并返回。
- `assert_reason_contains()` 先检查 `reason`，再调用 `strstr()`。
- `test_alarm_pose_json()` 在 `cJSON_Parse()` 和 `device_id` 校验失败时先清理资源再返回。

这样既保留了 Unity 测试语义，也让 cppcheck 能看懂控制流。

### 3. Unity / cJSON 宏组合带来的 informational 噪声

cppcheck 会对 `cJSON.h` 和 `unity_internals.h` 的多组宏配置做组合分析，因此出现：

```text
Too many #ifdef configurations
Limiting analysis of branches
```

这些属于分析配置提示，不是代码缺陷。由于 CI 启用了 `--error-exitcode=1`，本周在 `make cppcheck` 中显式 suppress：

```bash
--suppress=toomanyconfigs
--suppress=normalCheckLevelMaxBranches
```

同时增加：

```bash
-URUN_TEST
```

避免 cppcheck 在某些 Unity 宏分支下把 `RUN_TEST(...)` 误判为语法错误。

## 数据库临时文件清理

本周发现 SQLite 运行时生成的临时文件已经被误提交：

```text
rpi_app/database/gateway.db-shm
rpi_app/database/gateway.db-wal
```

这两个文件是 SQLite WAL 模式的运行时产物，不应该进入版本库。

处理方式：

```bash
git rm --cached rpi_app/database/gateway.db-shm rpi_app/database/gateway.db-wal
```

并在 `.gitignore` 中补充：

```gitignore
*.db-shm
*.db-wal
```

当前效果是：下一次提交会从远端仓库删除这两个临时文件，但保留本机文件；后续生成的新 WAL / SHM 文件也不会再被 Git 跟踪。

## 验证结果

本周最终在本地完成以下验证：

```bash
make cppcheck
make test
make all
make test-xml
bash -n scripts/driver_test.sh
git diff --check
```

结果：

| 命令 | 结果 |
|------|------|
| `make cppcheck` | 通过，12 个 rpi_app 源码/测试文件检查完成 |
| `make test` | 通过，`14 Tests 0 Failures 0 Ignored` |
| `make all` | 通过，正式程序 `build/edge_gatewayd` 链接成功 |
| `make test-xml` | 通过，生成 `build/test_report.xml` |
| `bash -n scripts/driver_test.sh` | 通过，脚本语法检查无错误 |
| sudo 日志最小验证 | 通过，`mktemp` 日志文件可正常 `tee -a` |
| `git diff --check` | 通过，无空白格式问题 |

说明：`driver_test.sh` 的完整 `insmod` / `rmmod` 流程依赖树莓派、root 权限、设备树 overlay 和 MPU6050 硬件，本地非硬件环境仅完成脚本静态语法检查和关键 dmesg 计数逻辑验证。

## 本周结论

第九周完成了从“手动测试”到“CI 质量门禁”的关键升级：

- 测试框架从裸 `assert()` 迁移到 Unity。
- 单元测试覆盖扩展到协议解析器边界场景。
- GitHub Actions 建立了单元测试、测试报告、ARM 交叉编译和静态分析四条检查线。
- `cppcheck` 的真实 const 问题已修复，Unity 宏误报已通过代码结构和参数处理。
- `Makefile` 的应用/测试链接职责已拆分，正式程序不再链接测试框架。
- `scripts/driver_test.sh` 覆盖了驱动构建、加载、设备节点、IIO 读取、dmesg 检查和卸载清理流程。
- SQLite 临时文件已从版本跟踪中移除，并通过 `.gitignore` 防止再次误提交。

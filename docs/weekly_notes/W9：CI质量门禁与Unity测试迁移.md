# W9：CI 质量门禁与 Unity 测试迁移

## 目标

引入 Unity 单元测试框架替换裸 `assert()`，补写协议解析器单测覆盖粘包/半包/错帧场景，为后续 GitHub Actions CI 流水线做好准备。

## 为什么要迁移到 Unity

| 对比项 | 裸 `assert()` | Unity |
|--------|---------------|-------|
| 失败行为 | 第一个失败直接 abort，后续测试全跳过 | 记录失败，继续执行剩余测试 |
| 输出格式 | 无统计，只有 PASSED/崩溃 | `14 Tests 0 Failures 0 Ignored`，CI 可解析 |
| 浮点比较 | 需手动写 epsilon 辅助函数 | `TEST_ASSERT_FLOAT_WITHIN` 内置支持 |
| 引入成本 | 无 | 3 个文件（unity.c / unity.h / unity_internals.h），放入 `external/` |

## 改动清单

### 测试文件

| 文件 | 改动 |
|------|------|
| `rpi_app/tests/test_main.c` | 全量重写：`assert()` → `TEST_ASSERT_*`，`main()` → `UNITY_BEGIN/UNITY_END + RUN_TEST` |
| `rpi_app/external/unity.c` | **新增**，Unity 框架源码 |
| `rpi_app/external/unity.h` | **新增**，Unity 框架头文件 |
| `rpi_app/external/unity_internals.h` | **新增**，Unity 内部实现 |

### 构建配置

| 文件 | 改动 |
|------|------|
| `rpi_app/Makefile` | CFLAGS 新增 `-DUNITY_INCLUDE_FLOAT` 启用浮点断言 |
| `.gitignore` | 移除 `external/` 规则，外部库需纳入版本管理 |

## 测试用例总览

### 原有测试（迁移到 Unity 风格）

| 测试函数 | 覆盖模块 |
|----------|----------|
| `test_crc16_known_value` | CRC16 校验计算 |
| `test_ringbuf_basic_ops` | 环形缓冲区读写、满溢、环绕 |
| `test_config_thresholds` | 配置文件解析（含阈值字段） |
| `test_config_file_missing` | 配置文件不存在的错误路径 |
| `test_gateway_pose_snapshot` | IIO sysfs 模拟读取 + 姿态计算 |
| `test_gateway_pose_check_threshold` | 用户态阈值判定（10 个子场景） |
| `test_ioctl_commands` | ioctl 命令码唯一性和魔数校验 |
| `test_alarm_pose_json` | 告警姿态 JSON 序列化 |

### 新增协议解析器单测

| 测试函数 | 场景 | 验证点 |
|----------|------|--------|
| `test_parse_valid_heartbeat` | 正常心跳帧 | 解析成功，type=0x01，缓冲区清空 |
| `test_parse_half_packet` | 半包（数据不完整） | 首次返回 false，补齐后解析成功 |
| `test_parse_sticky_packets` | 粘包（两帧连续） | 连续两次 parse 分别取出两帧 |
| `test_parse_crc_error` | CRC 校验失败 | 返回 false，拒绝错帧 |
| `test_parse_garbage_then_valid` | 垃圾数据 + 有效帧 | 跳过垃圾字节，找到包头后正确解析 |
| `test_parse_empty_buffer` | 空缓冲区 | 返回 false，不崩溃 |

## 帧构造辅助函数

测试中用 `build_frame()` 构造标准协议帧，协议格式：

```
[0xAA] [0x55] [len] [type] [payload...] [CRC16-L] [CRC16-H]
```

- `len` = payload_len + 2（包含 type 字段和 len 自身之后的计算方式）
- CRC16 覆盖从 `0xAA` 到 payload 末尾

## 构建验证

```
$ make test
14 Tests 0 Failures 0 Ignored
OK
```

零警告，零失败。

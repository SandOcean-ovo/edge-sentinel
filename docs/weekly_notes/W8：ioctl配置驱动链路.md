# W8：ioctl 配置驱动链路

## 目标

将告警阈值从配置文件写入 `gateway_monitor.ko` 内核驱动，通过 ioctl 接口实现用户→内核的参数下发，用 mutex 保护阈值数据的并发访问，同时在应用层保留兜底判断。

## 设计决策：为什么阈值要写入驱动？

| 层级 | 触发时机 | 优势 |
|------|----------|------|
| **内核态**（read_raw 中比较） | 每次 sysfs 读取 MPU6050 数据时 | `dmesg` 实时告警，不依赖用户态调度 |
| **用户态**（gateway_pose_check_threshold） | MCU 告警触发时 | 驱动未加载也能独立判断，防御性兜底 |

两层同时存在，内核态做主检测，用户态做兜底。

## 数据流

```
gateway.conf
  accel_peak_threshold = 1.5
  accel_rms_threshold  = 0.3
  gyro_threshold       = 30.0
       │
       ▼  load_config()
GatewayConfig_t 结构体
       │
       ├──→ ioctl(/dev/gateway_monitor, IOC_SET_PEAK_THR, &val)
       │         │
       │         ▼  内核态
       │     copy_from_user() → mutex_lock(&thr_lock) → 写入阈值
       │                                                   │
       │     read_raw(sysfs) ──→ mutex_lock(&thr_lock) ──→ abs_g > peak?
       │                         pr_warn("exceeds peak threshold!")
       │
       └──→ 用户态兜底
              gateway_pose_check_threshold(pose, peak, rms, gyro, &reason)
```

## 涉及文件

### 配置层

| 文件 | 改动 |
|------|------|
| `rpi_app/include/conf.h` | `GatewayConfig_t` 新增 `accel_peak_threshold`、`accel_rms_threshold`、`gyro_threshold` |
| `rpi_app/src/conf.c` | 解析三个新键 |
| `rpi_app/conf/gateway.conf` | 写入默认值 `1.5 / 0.3 / 30.0` |

### 驱动层

| 文件 | 改动 |
|------|------|
| `rpi_driver/gateway_monitor/gateway_monitor.c` | 新增 misc 设备 `/dev/gateway_monitor`、ioctl、mutex、read_raw 阈值比较 |
| `rpi_app/include/gateway_monitor_ioctl.h` | **新文件**，内核/用户态共享 ioctl 命令码 |

### 应用层

| 文件 | 改动 |
|------|------|
| `rpi_app/src/main.c` | `push_thresholds_to_driver()` 启动时 ioctl 下发阈值；`handle_alarm_event` 集成用户态兜底 |
| `rpi_app/src/gateway_pose.c` | `gateway_pose_check_threshold()` 用户态阈值比较 |
| `rpi_app/include/gateway_pose.h` | 声明兜底函数 |

## ioctl 命令码

```c
#define GATEWAY_MONITOR_IOC_MAGIC 'G'

#define GATEWAY_MONITOR_IOC_SET_PEAK_THR  _IOW('G', 1, float)   // 加速度峰值阈值 (g)
#define GATEWAY_MONITOR_IOC_SET_RMS_THR   _IOW('G', 2, float)   // 加速度 RMS 阈值 (g)
#define GATEWAY_MONITOR_IOC_SET_GYRO_THR  _IOW('G', 3, float)   // 角速度阈值 (deg/s)
```

设备节点：`/dev/gateway_monitor`

## 锁机制分析

`struct mutex thr_lock` 保护 `mpu6050_data` 中的三个阈值字段，两条并发路径：

1. **ioctl 写入路径**（用户态发起）
   ```
   copy_from_user(&val, arg, sizeof(float))
     → mutex_lock(&thr_lock)
     → data->accel_peak_thr = val
     → mutex_unlock(&thr_lock)
   ```

2. **read_raw 读取路径**（sysfs/应用态触发的硬件读取 + 阈值比较）
   ```
   regmap_read()  // 读硬件寄存器
     → mutex_lock(&thr_lock)
     → abs_g > data->accel_peak_thr ?
     → mutex_unlock(&thr_lock)
   ```

### 为什么用 mutex 而不是 spinlock

- ioctl 路径中 `copy_from_user` 可能触发缺页异常，导致进程睡眠
- spinlock 持有期间禁止睡眠，否则死锁
- mutex 允许持有者睡眠，适合此场景

## 用户态兜底

`gateway_pose_check_threshold()` 在 MCU 告警触发时被调用，直接从 `gateway_pose_t` 结构体比较阈值，不依赖驱动是否加载：

```c
int gateway_pose_check_threshold(const gateway_pose_t *pose,
                                 float peak_g, float rms_g, float gyro_dps,
                                 const char **reason);
```

返回值非 0 表示触发告警，`reason` 输出具体原因（"accel_x exceeds peak" / "rms exceeds threshold" / "tilt exceeds gyro threshold"）。

## dmesg 观测

驱动加载并完成 ioctl 下发后：

```
gateway_monitor: peak threshold set to 1.500 g
gateway_monitor: rms threshold set to 0.300 g
gateway_monitor: gyro threshold set to 30.0 deg/s
gateway_monitor: ACCEL ch0x3B raw=28000 (1.673 g) exceeds peak threshold 1.500 g
```

## 构建验证

- 应用层编译：零错误零警告
- 单元测试：CRC16 / RingBuf / GatewayPose / AlarmPose JSON 全部通过

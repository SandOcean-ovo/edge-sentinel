#!/bin/bash
#
# driver_test.sh — edge-sentinel 驱动自动化测试脚本
#
# 在树莓派上运行：构建 → insmod → 功能验证 → rmmod
# 用法：sudo ./scripts/driver_test.sh [--quick]
#   --quick    仅构建检查，跳过 insmod/rmmod（适用于无硬件环境）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DRIVER_DIR="$PROJECT_DIR/rpi_driver"
LOG_FILE="/tmp/edge_driver_test.log"
PASS=0
FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_section() { echo -e "\n${YELLOW}=== $1 ===${NC}" | tee -a "$LOG_FILE"; }
log_pass()  { echo -e "  ${GREEN}[PASS]${NC} $1" | tee -a "$LOG_FILE"; PASS=$((PASS + 1)); }
log_fail()  { echo -e "  ${RED}[FAIL]${NC} $1" | tee -a "$LOG_FILE"; FAIL=$((FAIL + 1)); }
log_info()  { echo "  [INFO] $1" | tee -a "$LOG_FILE"; }

check_cmd() {
    command -v "$1" >/dev/null 2>&1 || { log_fail "Missing command: $1"; exit 1; }
}

cleanup() {
    log_section "清理"
    # 按逆序卸载模块
    rmmod gateway_monitor 2>/dev/null && log_info "rmmod gateway_monitor" || true
    rmmod edge_alarm      2>/dev/null && log_info "rmmod edge_alarm"      || true

    # 删除设备节点（如果模块未通过 class_create 自动管理）
    rm -f /dev/edge_alarm /dev/gateway_monitor 2>/dev/null || true
}

# ─── 0. 环境检查 ───────────────────────────────────────────────
log_section "环境检查"
check_cmd make
check_cmd gcc

if [[ "${1:-}" == "--quick" ]]; then
    log_info "Quick mode: 仅构建检查，跳过内核操作"
fi

# ─── 1. 构建 edge_alarm.ko ─────────────────────────────────────
log_section "构建 edge_alarm 驱动"
cd "$DRIVER_DIR/edge_alarm"
make clean 2>&1 | tee -a "$LOG_FILE" || true
if make 2>&1 | tee -a "$LOG_FILE"; then
    log_pass "edge_alarm.ko 构建成功"
else
    log_fail "edge_alarm.ko 构建失败"
    exit 1
fi

# ─── 2. 构建 gateway_monitor.ko ─────────────────────────────────
log_section "构建 gateway_monitor 驱动"
cd "$DRIVER_DIR/gateway_monitor"
make clean 2>&1 | tee -a "$LOG_FILE" || true
if make 2>&1 | tee -a "$LOG_FILE"; then
    log_pass "gateway_monitor.ko 构建成功"
else
    log_fail "gateway_monitor.ko 构建失败"
    exit 1
fi

if [[ "${1:-}" == "--quick" ]]; then
    echo -e "\n${GREEN}构建检查完成: $PASS passed, $FAIL failed${NC}"
    exit $FAIL
fi

# ─── 3. 检查是否在 Pi 上运行 ────────────────────────────────────
log_section "平台检查"
if [[ "$(id -u)" -ne 0 ]]; then
    log_fail "必须以 root 运行（需要 insmod/rmmod 权限）"
    echo "  请使用: sudo $0"
    exit 1
fi

if ! grep -q "BCM\|bcm\|raspberry" /proc/cpuinfo 2>/dev/null; then
    log_info "非树莓派平台 — 跳过 insmod/rmmod 步骤"
    QUICK_MODE=1
else
    QUICK_MODE=0
fi

if [[ "$QUICK_MODE" -eq 1 ]]; then
    echo -e "\n${GREEN}构建检查完成: $PASS passed, $FAIL failed${NC}"
    exit $FAIL
fi

# ─── 4. 加载模块 ────────────────────────────────────────────────
log_section "加载内核模块"

cleanup

if insmod "$DRIVER_DIR/edge_alarm/edge_alarm.ko" 2>&1 | tee -a "$LOG_FILE"; then
    log_pass "insmod edge_alarm.ko"
else
    log_fail "insmod edge_alarm.ko（可能缺少设备树 overlay）"
    log_info "请先加载设备树: dtoverlay edge-mcu-alarm"
    exit 1
fi

sleep 0.5

if insmod "$DRIVER_DIR/gateway_monitor/gateway_monitor.ko" 2>&1 | tee -a "$LOG_FILE"; then
    log_pass "insmod gateway_monitor.ko"
else
    log_fail "insmod gateway_monitor.ko（可能缺少 I2C 设备）"
    log_info "请确保 MPU6050 已正确连接且设备树已加载"
    cleanup
    exit 1
fi

sleep 0.5

# ─── 5. 验证设备节点 ────────────────────────────────────────────
log_section "验证设备节点"

if [[ -c /dev/edge_alarm ]]; then
    log_pass "/dev/edge_alarm 存在"
else
    log_fail "/dev/edge_alarm 不存在"
fi

if [[ -c /dev/gateway_monitor ]]; then
    log_pass "/dev/gateway_monitor 存在"
else
    log_fail "/dev/gateway_monitor 不存在"
fi

# ─── 6. 测试 edge_alarm ─────────────────────────────────────────
log_section "测试 edge_alarm"

# 6a. 非阻塞读取（无中断触发时）
ALARM_FD=""
exec {ALARM_FD}<>/dev/edge_alarm 2>/dev/null && log_pass "打开 /dev/edge_alarm" || log_fail "打开 /dev/edge_alarm"

# 6b. 非阻塞读取应返回 EAGAIN
NBUF=""
if timeout 1 cat /dev/edge_alarm >/dev/null 2>&1; then
    log_info "/dev/edge_alarm 读取返回（非阻塞模式）"
else
    log_info "/dev/edge_alarm 读取超时（正常：设备阻塞等待中断）"
fi

exec {ALARM_FD}>&- 2>/dev/null || true

# ─── 7. 测试 gateway_monitor (IIO sysfs) ────────────────────────
log_section "测试 gateway_monitor (IIO)"

IIO_DEVICE_DIR=""
IIO_DIRS=(/sys/bus/iio/devices/iio:device*)
for dir in "${IIO_DIRS[@]}"; do
    if [[ -f "$dir/name" ]] && grep -q "mpu6050" "$dir/name" 2>/dev/null; then
        IIO_DEVICE_DIR="$dir"
        break
    fi
done

if [[ -n "$IIO_DEVICE_DIR" ]]; then
    log_pass "找到 IIO 设备: $IIO_DEVICE_DIR"

    # 读取加速度原始值
    for axis in x y z; do
        RAW_FILE="$IIO_DEVICE_DIR/in_accel_${axis}_raw"
        if [[ -f "$RAW_FILE" ]]; then
            VAL=$(cat "$RAW_FILE" 2>/dev/null || echo "ERR")
            log_info "accel_${axis}_raw = $VAL"
            log_pass "读取 in_accel_${axis}_raw"
        else
            log_fail "缺少 $RAW_FILE"
        fi
    done
else
    log_fail "未找到 MPU6050 IIO 设备"
fi

# ─── 8. 检查内核日志 ────────────────────────────────────────────
log_section "内核日志检查"

DMESG_OUT="$PROJECT_DIR/dmesg_after_test.log"
dmesg > "$DMESG_OUT"

ERROR_COUNT=$(grep -ci "error\|oops\|bug\|warning" "$DMESG_OUT" 2>/dev/null || echo "0")
log_info "dmesg 输出已保存至: $DMESG_OUT"
log_info "相关内核消息:"
grep -i "edge_alarm\|gateway_monitor\|mpu6050\|edge_mcu" "$DMESG_OUT" 2>/dev/null | head -20 | while read -r line; do
    echo "    $line"
done

if [[ "$ERROR_COUNT" -eq 0 ]]; then
    log_pass "dmesg 无严重错误"
else
    log_info "dmesg 共 $ERROR_COUNT 条可疑消息，请人工检查"
fi

# ─── 9. 卸载模块 ────────────────────────────────────────────────
log_section "卸载模块"

cleanup

sleep 0.5

# 验证模块已卸载
if ! lsmod | grep -q "edge_alarm"; then
    log_pass "edge_alarm 已卸载"
else
    log_fail "edge_alarm 仍在内存中"
fi

if ! lsmod | grep -q "gateway_monitor"; then
    log_pass "gateway_monitor 已卸载"
else
    log_fail "gateway_monitor 仍在内存中"
fi

# ─── 10. 报告 ───────────────────────────────────────────────────
log_section "测试结果汇总"
echo -e "  ${GREEN}通过: $PASS${NC}"
echo -e "  ${RED}失败: $FAIL${NC}"
echo "  日志: $LOG_FILE"

if [[ "$FAIL" -eq 0 ]]; then
    echo -e "\n${GREEN}全部测试通过!${NC}"
    exit 0
else
    echo -e "\n${RED}存在 $FAIL 个失败项${NC}"
    exit 1
fi

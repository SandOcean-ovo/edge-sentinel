#!/bin/bash
#
# Run gateway_monitor ioctl stress under a lockdep-enabled kernel.
#
# Usage:
#   sudo ./scripts/lockdep_stress.sh [-t threads] [-n loops] [-c cpu-list] [-R read-every]
#
# Example:
#   sudo ./scripts/lockdep_stress.sh -t 10 -n 10000 -c 0-3
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
STRESS_BIN="$BUILD_DIR/ioctl_stress"
REPORT_ROOT="$PROJECT_DIR/docs/test_reports"
THREADS=10
LOOPS=10000
CPU_LIST="0-3"
READ_EVERY=16
ALLOW_NO_LOCKDEP=0

usage() {
    cat <<EOF
Usage: sudo $0 [options]
  -t <count>    worker threads (default: $THREADS)
  -n <count>    loops per thread (default: $LOOPS)
  -c <list>     taskset CPU list (default: $CPU_LIST)
  -R <count>    IIO read interval passed to ioctl_stress (default: $READ_EVERY)
  -f            run even if CONFIG_PROVE_LOCKING is not enabled
  -h            show this help
EOF
}

while getopts "t:n:c:R:fh" opt; do
    case "$opt" in
        t) THREADS="$OPTARG" ;;
        n) LOOPS="$OPTARG" ;;
        c) CPU_LIST="$OPTARG" ;;
        R) READ_EVERY="$OPTARG" ;;
        f) ALLOW_NO_LOCKDEP=1 ;;
        h) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done

if [[ "$(id -u)" -ne 0 ]]; then
    echo "error: run with sudo so dmesg and device nodes are accessible" >&2
    exit 1
fi

check_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: missing command: $1" >&2
        exit 1
    }
}

check_cmd gcc
check_cmd taskset
check_cmd dmesg
check_cmd grep

find_kernel_config() {
    local uname_r
    uname_r="$(uname -r)"

    if [[ -r /proc/config.gz ]]; then
        echo "/proc/config.gz"
        return 0
    fi

    if [[ -r "/boot/config-$uname_r" ]]; then
        echo "/boot/config-$uname_r"
        return 0
    fi

    if [[ -r "/lib/modules/$uname_r/build/.config" ]]; then
        echo "/lib/modules/$uname_r/build/.config"
        return 0
    fi

    return 1
}

config_has() {
    local key="$1"
    local cfg="$2"

    if [[ "$cfg" == "/proc/config.gz" ]]; then
        zgrep -q "^${key}=y$" "$cfg"
    else
        grep -q "^${key}=y$" "$cfg"
    fi
}

config_line() {
    local key="$1"
    local cfg="$2"

    if [[ "$cfg" == "/proc/config.gz" ]]; then
        zgrep -E "^${key}=|^# ${key} is not set" "$cfg" || true
    else
        grep -E "^${key}=|^# ${key} is not set" "$cfg" || true
    fi
}

KERNEL_CONFIG="$(find_kernel_config || true)"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_DIR="$REPORT_ROOT/lockdep_$TIMESTAMP"
mkdir -p "$REPORT_DIR" "$BUILD_DIR"

{
    echo "kernel: $(uname -a)"
    if [[ -n "$KERNEL_CONFIG" ]]; then
        echo "config: $KERNEL_CONFIG"
        config_line CONFIG_LOCKDEP_SUPPORT "$KERNEL_CONFIG"
        config_line CONFIG_PROVE_LOCKING "$KERNEL_CONFIG"
        config_line CONFIG_DEBUG_LOCK_ALLOC "$KERNEL_CONFIG"
        config_line CONFIG_LOCK_STAT "$KERNEL_CONFIG"
    else
        echo "config: unavailable"
        echo "note: using /proc/lockdep presence as runtime lockdep evidence"
    fi
} | tee "$REPORT_DIR/kernel_config.txt"

LOCKDEP_READY=0
if [[ -n "$KERNEL_CONFIG" ]] && config_has CONFIG_PROVE_LOCKING "$KERNEL_CONFIG" && config_has CONFIG_DEBUG_LOCK_ALLOC "$KERNEL_CONFIG"; then
    LOCKDEP_READY=1
elif [[ -r /proc/lockdep && -r /proc/lockdep_stats ]]; then
    LOCKDEP_READY=1
fi

if [[ "$LOCKDEP_READY" -ne 1 ]]; then
    echo ""
    echo "warning: this kernel is not a lockdep validation kernel."
    echo "         CONFIG_PROVE_LOCKING and CONFIG_DEBUG_LOCK_ALLOC are required."
    echo "         Use -f only for a normal SMP baseline run."
    if [[ "$ALLOW_NO_LOCKDEP" -ne 1 ]]; then
        {
            echo "lockdep_ready: 0"
            echo "result: skipped"
            echo "reason: CONFIG_PROVE_LOCKING and CONFIG_DEBUG_LOCK_ALLOC are not enabled"
            echo "next_step: boot a kernel with CONFIG_PROVE_LOCKING=y and rerun this script"
        } | tee "$REPORT_DIR/summary.txt"
        echo "report dir: $REPORT_DIR"
        exit 2
    fi
fi

echo ""
echo "building ioctl_stress..."
gcc -Wall -Wextra -O2 -g -pthread \
    -I"$PROJECT_DIR/rpi_app/include" \
    "$PROJECT_DIR/scripts/ioctl_stress.c" \
    -o "$STRESS_BIN"

DMESG_BEFORE_LINES="$(dmesg | wc -l)"
STRESS_LOG="$REPORT_DIR/ioctl_stress.log"
DMESG_AFTER="$REPORT_DIR/dmesg_after.log"
DMESG_DELTA="$REPORT_DIR/dmesg_delta.log"
SUSPECT_LOG="$REPORT_DIR/dmesg_suspect.log"
SUMMARY="$REPORT_DIR/summary.txt"

echo ""
echo "running stress: taskset -c $CPU_LIST $STRESS_BIN -t $THREADS -n $LOOPS -R $READ_EVERY"
set +e
taskset -c "$CPU_LIST" "$STRESS_BIN" -t "$THREADS" -n "$LOOPS" -R "$READ_EVERY" 2>&1 | tee "$STRESS_LOG"
STRESS_EXIT="${PIPESTATUS[0]}"
set -e

dmesg -T > "$DMESG_AFTER"
tail -n +"$((DMESG_BEFORE_LINES + 1))" "$DMESG_AFTER" > "$DMESG_DELTA"

grep -Ei "BUG:|WARNING:|Oops|Call Trace|lockdep|deadlock|possible circular locking dependency|possible recursive locking|KASAN|kmemleak|rcu stall" \
    "$DMESG_DELTA" > "$SUSPECT_LOG" || true

IOCTL_ERRORS="$(awk '/ioctl_errors:/ {print $2}' "$STRESS_LOG" | tail -1)"
VERIFY_ERRORS="$(awk '/verify_errors:/ {print $2}' "$STRESS_LOG" | tail -1)"
IIO_ERRORS="$(awk '/iio_errors:/ {print $2}' "$STRESS_LOG" | tail -1)"
SUSPECT_LINES="$(wc -l < "$SUSPECT_LOG")"

{
    echo "lockdep_ready: $LOCKDEP_READY"
    echo "threads: $THREADS"
    echo "loops_per_thread: $LOOPS"
    echo "cpu_list: $CPU_LIST"
    echo "read_every: $READ_EVERY"
    echo "stress_exit: $STRESS_EXIT"
    echo "ioctl_errors: ${IOCTL_ERRORS:-unknown}"
    echo "verify_errors: ${VERIFY_ERRORS:-unknown}"
    echo "iio_errors: ${IIO_ERRORS:-unknown}"
    echo "dmesg_suspect_lines: $SUSPECT_LINES"
    echo "stress_log: $STRESS_LOG"
    echo "dmesg_delta: $DMESG_DELTA"
    echo "dmesg_suspect: $SUSPECT_LOG"
} | tee "$SUMMARY"

echo ""
if [[ "$STRESS_EXIT" == "0" && "${IOCTL_ERRORS:-1}" == "0" && "${VERIFY_ERRORS:-1}" == "0" && "${IIO_ERRORS:-1}" == "0" && "$SUSPECT_LINES" -eq 0 ]]; then
    if [[ "$LOCKDEP_READY" -eq 1 ]]; then
        echo "PASS: lockdep stress completed with zero userspace errors and zero suspicious dmesg lines."
    else
        echo "PASS: normal SMP stress completed with zero userspace errors and zero suspicious dmesg lines."
        echo "      Re-run on CONFIG_PROVE_LOCKING=y for the real lockdep milestone."
    fi
    exit 0
fi

echo "FAIL: inspect $REPORT_DIR"
exit 1

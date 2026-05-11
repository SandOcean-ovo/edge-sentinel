#!/bin/bash

if [ $# -lt 2 ]; then
    echo "Usage: $0 <PID> <IRQ>"
    echo "  PID   edge_gatewayd process ID"
    echo "  IRQ   edge_alarm interrupt number"
    exit 1
fi

PID=$1
IRQ=$2

TRACE=/sys/kernel/debug/tracing
SAVE_DIR="$(pwd)"

cd $TRACE || exit 1

echo 0 > tracing_on
echo nop > current_tracer
echo > trace

# 清空所有过滤条件
echo > set_ftrace_filter
echo > set_graph_function
echo > set_ftrace_pid
echo > set_event_pid

# 关闭所有事件，无噪音
echo 0 > events/enable

# 只打开我们需要的事件
echo 1 > events/irq/irq_handler_entry/enable
echo 1 > events/irq/irq_handler_exit/enable
echo 1 > events/sched/sched_waking/enable
echo 1 > events/sched/sched_wakeup/enable
echo 1 > events/sched/sched_switch/enable

# 只捕获目标 IRQ
echo "irq == $IRQ" > events/irq/irq_handler_entry/filter
echo "irq == $IRQ" > events/irq/irq_handler_exit/filter

# 只捕获目标进程
echo "pid == $PID" > events/sched/sched_waking/filter
echo "pid == $PID" > events/sched/sched_wakeup/filter
echo "next_pid == $PID || prev_pid == $PID" > events/sched/sched_switch/filter

# 时钟源：global 保证跨 CPU 单调一致
echo global > trace_clock

# 缓冲区大小
echo 4096 > buffer_size_kb

# ---------- SIGINT / SIGTERM 处理 ----------
cleanup() {
    echo ""
    echo 0 > $TRACE/tracing_on
    echo "Saving trace to $SAVE_DIR/trace.txt ..."
    cat $TRACE/trace > "$SAVE_DIR/trace.txt"
    echo "Done."
    exit 0
}
trap cleanup INT TERM

# 开始追踪
echo 1 > tracing_on
echo "Tracing started! PID=$PID, IRQ=$IRQ"
echo "Press Ctrl+C to stop and save trace to trace.txt"

# 挂起等待信号
sleep infinity &
wait

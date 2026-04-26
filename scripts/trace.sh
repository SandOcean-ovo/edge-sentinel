#!/bin/sh

TRACE=/sys/kernel/debug/tracing
PID=134131    # 你的进程PID
IRQ=186       # 你要抓的中断号

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

# 只捕获 IRQ 186
echo "irq == $IRQ" > events/irq/irq_handler_entry/filter
echo "irq == $IRQ" > events/irq/irq_handler_exit/filter

# 只捕获你的进程
echo "pid == $PID" > events/sched/sched_waking/filter
echo "pid == $PID" > events/sched/sched_wakeup/filter
echo "next_pid == $PID || prev_pid == $PID" > events/sched/sched_switch/filter

# 缓冲区大小
echo 4096 > buffer_size_kb

# 开始追踪
echo 1 > tracing_on

echo "✅ 追踪已启动！PID=$PID, IRQ=$IRQ"

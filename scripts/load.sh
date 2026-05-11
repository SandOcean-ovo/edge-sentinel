#!/bin/bash
# 自动加载依赖
sudo modprobe industrialio
sudo modprobe regmap-i2c
# 加载自己的模块
sudo insmod ../rpi_driver/gateway_monitor/gateway_monitor.ko

if [ $? -eq 0 ]; then
    echo "模块加载成功！"
else
    echo "加载失败，请检查 dmesg"
fi
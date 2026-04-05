#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 振动监测节点 - 串口数据解析工具
"""

import serial
import struct
import time
from datetime import datetime

# 协议常量
PROTOCOL_SOF_0 = 0xAA
PROTOCOL_SOF_1 = 0x55

# 消息类型
MSG_TYPE_HEARTBEAT = 0x01
MSG_TYPE_IMU_FEATURE = 0x02
MSG_TYPE_ENVIRONMENT = 0x03
MSG_TYPE_ALARM = 0x04
MSG_TYPE_CONFIG_DOWN = 0x10
MSG_TYPE_CONFIG_ACK = 0x11
MSG_TYPE_STATUS_QUERY = 0x12

MSG_TYPE_NAMES = {
    0x01: "心跳",
    0x02: "IMU特征",
    0x03: "环境",
    0x04: "告警",
    0x10: "配置下发",
    0x11: "配置ACK",
    0x12: "状态查询"
}

# 告警类型
ALARM_TYPE_BUTTON = 0x01
ALARM_TYPE_PEAK = 0x02
ALARM_TYPE_RMS = 0x04
ALARM_TYPE_GYRO = 0x08


def calc_crc16(data):
    """计算 CRC16 (CRC-16/XMODEM)"""
    crc = 0x0000
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


def parse_frame(data):
    """解析协议帧"""
    if len(data) < 6:  # 最小帧长度：SOF(2) + Len(1) + Type(1) + CRC(2)
        return None

    # 检查帧头
    if data[0] != PROTOCOL_SOF_0 or data[1] != PROTOCOL_SOF_1:
        return None

    frame_len = data[2]  # Payload + CRC
    msg_type = data[3]
    payload_len = frame_len - 2

    # 检查长度
    if len(data) < 4 + payload_len + 2:
        return None

    # 提取载荷和 CRC
    payload = data[4:4+payload_len]
    recv_crc = (data[4+payload_len] << 8) | data[5+payload_len]

    # 计算 CRC
    calc_crc = calc_crc16(data[:4+payload_len])

    # 校验 CRC
    if recv_crc != calc_crc:
        print(f"[错误] CRC 校验失败: 接收={recv_crc:04X}, 计算={calc_crc:04X}")
        return None

    return {
        'type': msg_type,
        'payload': payload,
        'frame_len': 4 + payload_len + 2
    }


def parse_imu_feature(payload):
    """解析 IMU 特征数据"""
    if len(payload) < 21:
        return None

    # 小端序解析 5 个 float + 1 个 uint8
    accel_peak, accel_rms, gyro_x, gyro_y, gyro_z = struct.unpack('<5f', payload[:20])
    alarm_flag = payload[20]

    return {
        'accel_peak': accel_peak,
        'accel_rms': accel_rms,
        'gyro_x': gyro_x,
        'gyro_y': gyro_y,
        'gyro_z': gyro_z,
        'alarm_flag': alarm_flag
    }


def parse_alarm(payload):
    """解析告警数据"""
    if len(payload) < 5:
        return None

    alarm_type = payload[0]
    timestamp = struct.unpack('<I', payload[1:5])[0]

    alarm_types = []
    if alarm_type & ALARM_TYPE_BUTTON:
        alarm_types.append("按键")
    if alarm_type & ALARM_TYPE_PEAK:
        alarm_types.append("峰值超限")
    if alarm_type & ALARM_TYPE_RMS:
        alarm_types.append("RMS超限")
    if alarm_type & ALARM_TYPE_GYRO:
        alarm_types.append("角速度超限")

    return {
        'alarm_type': alarm_type,
        'alarm_types': alarm_types,
        'timestamp': timestamp
    }


def print_imu_feature(data):
    """打印 IMU 特征数据"""
    print(f"  加速度峰值: {data['accel_peak']:.4f} g")
    print(f"  加速度RMS:  {data['accel_rms']:.4f} g")
    print(f"  角速度X:    {data['gyro_x']:.2f} deg/s")
    print(f"  角速度Y:    {data['gyro_y']:.2f} deg/s")
    print(f"  角速度Z:    {data['gyro_z']:.2f} deg/s")

    if data['alarm_flag']:
        alarm_str = []
        if data['alarm_flag'] & 0x01:
            alarm_str.append("峰值")
        if data['alarm_flag'] & 0x02:
            alarm_str.append("RMS")
        if data['alarm_flag'] & 0x04:
            alarm_str.append("角速度")
        print(f"  ⚠️  告警标志: {' + '.join(alarm_str)}")
    else:
        print(f"  ✅ 无告警")


def print_alarm(data):
    """打印告警数据"""
    print(f"  告警类型: {' + '.join(data['alarm_types'])}")
    print(f"  时间戳:   {data['timestamp']} ms")


def main():
    """主函数"""
    # 配置串口参数
    PORT = 'COM11'  # 根据实际情况修改
    BAUDRATE = 115200

    print(f"正在打开串口 {PORT} @ {BAUDRATE} bps...")

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        print(f"串口已打开，开始接收数据...\n")

        buffer = bytearray()

        while True:
            # 读取数据
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                buffer.extend(data)

                # 查找帧头
                while len(buffer) >= 6:
                    # 查找 0xAA 0x55
                    idx = -1
                    for i in range(len(buffer) - 1):
                        if buffer[i] == PROTOCOL_SOF_0 and buffer[i+1] == PROTOCOL_SOF_1:
                            idx = i
                            break

                    if idx == -1:
                        # 没有找到帧头，清空缓冲区
                        buffer.clear()
                        break

                    # 丢弃帧头之前的数据
                    if idx > 0:
                        buffer = buffer[idx:]

                    # 尝试解析帧
                    frame = parse_frame(buffer)

                    if frame is None:
                        # 解析失败，可能数据不完整，等待更多数据
                        if len(buffer) > 256:
                            # 缓冲区太大，可能出错，丢弃第一个字节
                            buffer = buffer[1:]
                        break

                    # 解析成功
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    msg_type_name = MSG_TYPE_NAMES.get(frame['type'], f"未知(0x{frame['type']:02X})")

                    print(f"[{timestamp}] 收到消息: {msg_type_name}")

                    # 根据消息类型解析载荷
                    if frame['type'] == MSG_TYPE_HEARTBEAT:
                        print("  💓 心跳包")

                    elif frame['type'] == MSG_TYPE_IMU_FEATURE:
                        imu_data = parse_imu_feature(frame['payload'])
                        if imu_data:
                            print_imu_feature(imu_data)

                    elif frame['type'] == MSG_TYPE_ALARM:
                        alarm_data = parse_alarm(frame['payload'])
                        if alarm_data:
                            print_alarm(alarm_data)

                    print()

                    # 移除已处理的帧
                    buffer = buffer[frame['frame_len']:]

            time.sleep(0.01)

    except serial.SerialException as e:
        print(f"串口错误: {e}")
    except KeyboardInterrupt:
        print("\n程序已停止")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("串口已关闭")


if __name__ == '__main__':
    main()

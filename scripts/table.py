def generate_crc16_modbus_tables():
    # CRC16-Modbus 反向多项式
    poly = 0xA001
    
    table_high = []
    table_low = []

    for i in range(256):
        crc = i
        # 对当前字节进行 8 次位移计算
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
        
        # 将 16 位结果拆分为高位和低位
        # Modbus 查表法通常存储的是该字节对应的偏移量
        table_high.append((crc >> 8) & 0xFF)
        table_low.append(crc & 0xFF)

    return table_high, table_low

# 执行生成
high_table, low_table = generate_crc16_modbus_tables()

# 格式化输出，方便复制到 C/C++ 代码中
def print_table(name, data):
    print(f"/* {name} */")
    print("const unsigned char " + name + "[] = {")
    for i in range(0, 256, 16):
        line = ", ".join([f"0x{x:02X}" for x in data[i:i+16]])
        print(f"    {line},")
    print("};\n")

print_table("auchCRCHi", high_table)
print_table("auchCRCLo", low_table)
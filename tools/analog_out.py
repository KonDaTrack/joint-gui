#!/usr/bin/env python3
# RS485 控制模拟量模块输出 0-10V（Modbus RTU 功能码 06 写单个保持寄存器）
# 用法: python3 analog_out.py 5.0    # 输出 5.0V
# 顶部 5 个参数按模块手册改（见 QUICKSTART.md 表格）。
import sys, time
import serial

PORT      = "/dev/ttyS5"   # 板载 RS485 口（ls /dev/tty* 找）；USB 转 RS485 用 /dev/ttyUSB0
BAUD      = 9600
DEVICE_ID = 1              # Modbus 从站地址
REGISTER  = 0x0000         # 输出通道保持寄存器
SCALE     = 1000.0         # 满量程值 = 电压 × SCALE（10V→10000 则 SCALE=1000）
MAX_V     = 10.0


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def write_reg(ser, dev: int, reg: int, val: int) -> None:
    frame = bytes([dev, 0x06, (reg >> 8) & 0xFF, reg & 0xFF,
                   (val >> 8) & 0xFF, val & 0xFF])
    crc = crc16(frame)
    ser.write(frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF]))
    time.sleep(0.1)


def main():
    v = float(sys.argv[1]) if len(sys.argv) > 1 else 0.0
    v = max(0.0, min(MAX_V, v))
    val = int(round(v * SCALE))
    with serial.Serial(PORT, BAUD, timeout=0.5) as ser:
        write_reg(ser, DEVICE_ID, REGISTER, val)
    print(f"已写: {v:.2f}V -> 寄存器 0x{REGISTER:04X} 值 {val}")


if __name__ == "__main__":
    main()

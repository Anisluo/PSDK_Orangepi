#!/usr/bin/env python3
"""
uart_test.py — 直接读写 ttyACM0，验证 DJI E-Port UART 通道是否有数据
用法: python3 uart_test.py [设备] [波特率]
示例: python3 uart_test.py /dev/ttyACM0 460800
"""

import sys
import os
import time
import termios
import tty
import select

DEV   = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD  = int(sys.argv[2]) if len(sys.argv) > 2 else 460800

# 波特率映射
BAUD_MAP = {
    9600:   termios.B9600,
    115200: termios.B115200,
    460800: termios.B460800,
    921600: termios.B921600,
}

def open_serial(dev, baud):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    # cfmakeraw equivalent
    attrs[0] = 0  # iflag
    attrs[1] = 0  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0  # lflag
    attrs[4] = BAUD_MAP.get(baud, termios.B460800)  # ispeed
    attrs[5] = BAUD_MAP.get(baud, termios.B460800)  # ospeed
    attrs[6][termios.VMIN]  = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

# DJI PSDK V2 帧头 — 0xAA + 最小合法帧（仅作刺探用）
PSDK_PROBE = bytes([
    0xAA,       # SOF
    0x13, 0x00, # Ver(1) + Len(18 bytes total), little-endian 10-bit
    0x00,       # CRC8 placeholder
    0x00, 0x00, # SessionID / EncType
    0x00, 0x00, # SeqNum
    0x00, 0x00, 0x00, 0x00,  # CRC32 placeholder
])

def main():
    print(f"[uart_test] 打开 {DEV} @ {BAUD} baud")
    try:
        fd = open_serial(DEV, BAUD)
    except Exception as e:
        print(f"[ERROR] 无法打开设备: {e}")
        sys.exit(1)

    print(f"[uart_test] 设备已打开，fd={fd}")
    print("[uart_test] 先静默监听 3 秒，看无人机是否主动发数据...")

    received = bytearray()
    deadline = time.time() + 3.0
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            chunk = os.read(fd, 256)
            if chunk:
                received.extend(chunk)
                print(f"  收到 {len(chunk)} 字节: {chunk.hex(' ')}")

    if received:
        print(f"\n[OK] 静默阶段共收到 {len(received)} 字节 — UART 通道有数据！")
    else:
        print("\n[INFO] 静默阶段无数据，开始主动发送探测帧...")

        for i in range(5):
            os.write(fd, PSDK_PROBE)
            print(f"  发送探测帧 #{i+1}: {PSDK_PROBE.hex(' ')}")
            time.sleep(0.5)
            r, _, _ = select.select([fd], [], [], 0.5)
            if r:
                chunk = os.read(fd, 256)
                if chunk:
                    received.extend(chunk)
                    print(f"  << 收到回复 {len(chunk)} 字节: {chunk.hex(' ')}")

    print()
    if received:
        print(f"[结论] UART 通道正常，共收到 {len(received)} 字节")
        print(f"       原始数据: {received.hex(' ')}")
    else:
        print("[结论] UART 通道无任何响应")
        print("       可能原因:")
        print("       1. 无人机未进入 E-Port/PSDK 模式")
        print("       2. 波特率不匹配（当前: {})".format(BAUD))
        print("       3. USB 线缆问题（非数据线）")

    os.close(fd)

if __name__ == "__main__":
    main()

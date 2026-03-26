#!/usr/bin/env python3
"""
prepare_uart.py -- precondition a serial device for M3E PSDK startup

Best-effort steps:
1. Set the tty into raw 8N1 mode at the requested baudrate.
2. Assert DTR/RTS.
3. Flush pending input/output.
4. Try to enable ASYNC_LOW_LATENCY via TIOCSSERIAL.
5. Force runtime PM to "on" for the tty's USB device if sysfs exposes it.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import fcntl
import os
import sys
import termios


TIOCMGET = termios.TIOCMGET
TIOCMSET = termios.TIOCMSET
TIOCGSERIAL = termios.TIOCGSERIAL
TIOCSSERIAL = termios.TIOCSSERIAL
ASYNC_LOW_LATENCY = 0x2000


class SerialStruct(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("line", ctypes.c_int),
        ("port", ctypes.c_uint),
        ("irq", ctypes.c_int),
        ("flags", ctypes.c_int),
        ("xmit_fifo_size", ctypes.c_int),
        ("custom_divisor", ctypes.c_int),
        ("baud_base", ctypes.c_int),
        ("close_delay", ctypes.c_ushort),
        ("io_type", ctypes.c_ubyte),
        ("reserved_char", ctypes.c_ubyte),
        ("hub6", ctypes.c_int),
        ("closing_wait", ctypes.c_ushort),
        ("closing_wait2", ctypes.c_ushort),
        ("iomem_base", ctypes.c_void_p),
        ("iomem_reg_shift", ctypes.c_ushort),
        ("port_high", ctypes.c_uint),
        ("iomap_base", ctypes.c_ulong),
    ]


def log(message: str) -> None:
    print(f"[prepare_uart] {message}")


def set_power_control(dev: str) -> None:
    real = os.path.realpath(f"/sys/class/tty/{os.path.basename(dev)}/device")
    candidates = [
        os.path.join(real, "power", "control"),
        os.path.normpath(os.path.join(real, "..", "power", "control")),
    ]

    for path in candidates:
        if not os.path.exists(path):
            continue
        try:
            with open(path, "w", encoding="ascii") as fh:
                fh.write("on\n")
            log(f"set runtime PM to on: {path}")
            return
        except OSError as exc:
            log(f"runtime PM update skipped ({path}: {exc})")
            return


def set_low_latency(fd: int) -> None:
    buf = bytearray(ctypes.sizeof(SerialStruct))
    try:
        fcntl.ioctl(fd, TIOCGSERIAL, buf, True)
    except OSError as exc:
        log(f"TIOCGSERIAL unavailable: {exc}")
        return

    serial = SerialStruct.from_buffer(buf)
    if serial.flags & ASYNC_LOW_LATENCY:
        log("ASYNC_LOW_LATENCY already enabled")
        return

    serial.flags |= ASYNC_LOW_LATENCY
    try:
        fcntl.ioctl(fd, TIOCSSERIAL, buf)
        log("enabled ASYNC_LOW_LATENCY")
    except OSError as exc:
        log(f"TIOCSSERIAL skipped: {exc}")


def configure_uart(dev: str, baud_attr: str) -> None:
    baud = getattr(termios, baud_attr, None)
    if baud is None:
        raise ValueError(f"unsupported baud attribute: {baud_attr}")

    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                      termios.ISTRIP | termios.INLCR | termios.IGNCR |
                      termios.ICRNL | termios.IXON | termios.IXOFF | termios.IXANY)
        attrs[1] &= ~termios.OPOST
        attrs[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
        attrs[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
        attrs[3] &= ~(termios.ECHO | termios.ECHOE | termios.ECHONL |
                      termios.ICANON | termios.ISIG | termios.IEXTEN)
        attrs[4] = baud
        attrs[5] = baud
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcflush(fd, termios.TCIOFLUSH)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

        modem_bits = ctypes.c_int()
        fcntl.ioctl(fd, TIOCMGET, modem_bits, True)
        modem_bits.value |= termios.TIOCM_DTR | termios.TIOCM_RTS
        fcntl.ioctl(fd, TIOCMSET, modem_bits)

        set_low_latency(fd)
        log(f"configured {dev} to raw mode @ {baud_attr}")
    finally:
        os.close(fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("device")
    parser.add_argument("--baud", default="B921600")
    args = parser.parse_args()

    if not os.path.exists(args.device):
        log(f"device missing: {args.device}")
        return 1

    try:
        set_power_control(args.device)
        configure_uart(args.device, args.baud)
    except OSError as exc:
        if exc.errno == errno.EACCES:
            log(f"permission denied: {exc}")
        else:
            log(f"configuration failed: {exc}")
        return 1
    except ValueError as exc:
        log(str(exc))
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/bin/bash
# start_dual.sh — PSDK 双通道启动：物理 UART (ttyUSB0/CH340) + USB RNDIS 网络
# libpayloadsdk.a aarch64 = DJI_USE_UART_AND_NETWORK_DEVICE 模式
# M3E 作为 USB HOST，枚举 OrangePi RNDIS gadget 并通过 DHCP 分配 IP
# 用法: sudo bash start_dual.sh
set -e

PSDK_DIR=/home/orangepi/PSDK
GADGET=/sys/kernel/config/usb_gadget/psdk
UDC_FILE=$GADGET/UDC
UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)

echo "=== PSDK 启动 (UART=ttyUSB0/CH340 + USB RNDIS) ==="

# 1. 清理
echo "[1] 清理旧进程..."
pkill -f psdkd 2>/dev/null || true
pkill -f ffs_init 2>/dev/null || true
sleep 0.3

# 解绑 UDC
echo "" > $UDC_FILE 2>/dev/null || true
sleep 0.3

# 2. 重建 gadget：RNDIS 网络 (匹配 Manifold2 VID/PID = 0x0955/0x7020)
echo "[2] 配置 USB Gadget (RNDIS 网络)..."
modprobe libcomposite usb_f_rndis 2>/dev/null || true

# 删除旧 gadget
if [ -d $GADGET ]; then
    rm -f $GADGET/configs/c.1/rndis.usb0 2>/dev/null || true
    rm -f $GADGET/configs/c.1/ffs.bulk1  2>/dev/null || true
    rm -f $GADGET/configs/c.1/acm.GS0   2>/dev/null || true
    rmdir $GADGET/functions/rndis.usb0   2>/dev/null || true
    rmdir $GADGET/functions/ffs.bulk1    2>/dev/null || true
    rmdir $GADGET/functions/acm.GS0     2>/dev/null || true
    rmdir $GADGET/configs/c.1/strings/0x409 2>/dev/null || true
    rmdir $GADGET/configs/c.1           2>/dev/null || true
    rmdir $GADGET/strings/0x409         2>/dev/null || true
    rmdir $GADGET                       2>/dev/null || true
fi

mkdir -p $GADGET
echo 0x0955 > $GADGET/idVendor   # NVIDIA (Manifold2)
echo 0x7020 > $GADGET/idProduct  # Manifold2 composite device
mkdir -p $GADGET/strings/0x409
echo "Manifold2" > $GADGET/strings/0x409/product
echo "DJI00000001" > $GADGET/strings/0x409/serialnumber
mkdir -p $GADGET/configs/c.1
mkdir -p $GADGET/configs/c.1/strings/0x409
echo "PSDK" > $GADGET/configs/c.1/strings/0x409/configuration

# RNDIS 接口 (USB 网络)
mkdir -p $GADGET/functions/rndis.usb0
ln -s $GADGET/functions/rndis.usb0 $GADGET/configs/c.1/

# 3. 绑定 UDC
echo "[3] 绑定 UDC: $UDC"
echo "$UDC" > $UDC_FILE
echo "UDC state: $(cat /sys/class/udc/$UDC/state 2>/dev/null)"

echo ""
echo ">>> 现在请把 OrangePi USB-C 接到 M3E E-Port USB 口 <<<"
echo ">>> 等待 M3E 枚举 OrangePi RNDIS (最多 15 秒)..."
echo ""

# 4. 等待 usb0 接口出现（M3E DHCP 后会出现）
for i in $(seq 1 30); do
    if ip link show usb0 > /dev/null 2>&1; then
        echo "[4] usb0 接口已出现! M3E 已连接."
        break
    fi
    echo "  等待 usb0... ($i/30)"
    sleep 0.5
done

if ! ip link show usb0 > /dev/null 2>&1; then
    echo "ERROR: usb0 未出现，M3E 未连接或 USB 线有问题"
    exit 1
fi

# 启用接口；预配 payload 端 IP（SDK 会在 HalNetWork_Init 里再 ifconfig 一次）
ip link set usb0 up 2>/dev/null || true
ip addr flush dev usb0 2>/dev/null || true
ip addr add 192.168.5.3/24 dev usb0 2>/dev/null || true
echo "[5] usb0 已配置:"
ip addr show usb0

echo "[6] 启动 psdkd..."
exec $PSDK_DIR/build/bin/psdkd --debug

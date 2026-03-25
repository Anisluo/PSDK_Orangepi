#!/bin/bash
# setup_gadget.sh — 配置 OrangePi 为 USB Gadget (Device) 模式
# 供 DJI M3E E-Port PSDK 使用
#
# 必须以 root 运行: sudo ./tools/setup_gadget.sh
#
# 配置内容:
#   - CDC-ACM 串口  → /dev/ttyGS0   (PSDK UART 通道)
#   - USB Bulk FunctionFS → /dev/usb-ffs/bulk1, bulk2  (PSDK USB Bulk 通道)
#
# 使用 DJI VID/PID 让飞机识别为 PSDK 负载设备
#   VID = 0x2CA3 (DJI)
#   PID = 0x001F (M3E E-Port payload)

set -e
UDC_BIND_LATER=1   # UDC binding done by start.sh after psdkd initialises FunctionFS ep0

GADGET_DIR=/sys/kernel/config/usb_gadget/psdk

# ── 1. 检查 UDC ──────────────────────────────────────────────────────────────
UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
if [ -z "$UDC" ]; then
    echo "[ERROR] 未找到 UDC 控制器。请确认:"
    echo "  1. OrangePi USB-C 口支持 OTG/Device 模式"
    echo "  2. 内核已加载 dwc3 或 musb 驱动"
    echo "  modprobe dwc3-generic 或检查 /boot/orangepiEnv.txt"
    exit 1
fi
echo "[gadget] UDC: $UDC"

# ── 2. 加载 libcomposite ─────────────────────────────────────────────────────
modprobe libcomposite
modprobe usb_f_acm
modprobe usb_f_fs

# ── 3. 挂载 configfs ─────────────────────────────────────────────────────────
if ! mountpoint -q /sys/kernel/config; then
    mount -t configfs none /sys/kernel/config
fi

# ── 4. 如果已存在则先清理 ────────────────────────────────────────────────────
if [ -d "$GADGET_DIR" ]; then
    echo "[gadget] 清理旧配置..."
    # 先解绑 UDC
    echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
    sleep 0.2
    # 删除函数绑定
    rm -f "$GADGET_DIR"/configs/c.1/acm.GS0 2>/dev/null || true
    rm -f "$GADGET_DIR"/configs/c.1/ffs.bulk1 2>/dev/null || true
    # 删除函数
    rmdir "$GADGET_DIR"/functions/acm.GS0 2>/dev/null || true
    rmdir "$GADGET_DIR"/functions/ffs.bulk1 2>/dev/null || true
    # 删除配置
    rmdir "$GADGET_DIR"/configs/c.1/strings/0x409 2>/dev/null || true
    rmdir "$GADGET_DIR"/configs/c.1 2>/dev/null || true
    # 删除字符串
    rmdir "$GADGET_DIR"/strings/0x409 2>/dev/null || true
    rmdir "$GADGET_DIR" 2>/dev/null || true
    sleep 0.3
fi

# ── 5. 创建 gadget ───────────────────────────────────────────────────────────
mkdir -p "$GADGET_DIR"
echo 0x2CA3 > "$GADGET_DIR/idVendor"   # DJI
echo 0x001F > "$GADGET_DIR/idProduct"  # M3E payload
echo 0x0100 > "$GADGET_DIR/bcdDevice"
echo 0x0200 > "$GADGET_DIR/bcdUSB"     # USB 2.0

mkdir -p "$GADGET_DIR/strings/0x409"
echo "DJI"            > "$GADGET_DIR/strings/0x409/manufacturer"
echo "PSDK Payload"   > "$GADGET_DIR/strings/0x409/product"
echo "PSDK0001"       > "$GADGET_DIR/strings/0x409/serialnumber"

# ── 6. 配置 ─────────────────────────────────────────────────────────────────
mkdir -p "$GADGET_DIR/configs/c.1"
echo 500 > "$GADGET_DIR/configs/c.1/MaxPower"   # 500mA

mkdir -p "$GADGET_DIR/configs/c.1/strings/0x409"
echo "PSDK Config" > "$GADGET_DIR/configs/c.1/strings/0x409/configuration"

# ── 7. CDC-ACM 串口函数 (→ /dev/ttyGS0) ─────────────────────────────────────
mkdir -p "$GADGET_DIR/functions/acm.GS0"
ln -s "$GADGET_DIR/functions/acm.GS0" "$GADGET_DIR/configs/c.1/acm.GS0"
echo "[gadget] CDC-ACM 串口函数已添加 → /dev/ttyGS0"

# ── 8. USB Bulk FunctionFS (→ /dev/usb-ffs/bulk1) ──────────────────────────
mkdir -p "$GADGET_DIR/functions/ffs.bulk1"
ln -s "$GADGET_DIR/functions/ffs.bulk1" "$GADGET_DIR/configs/c.1/ffs.bulk1"

# 挂载 FunctionFS
mkdir -p /dev/usb-ffs/bulk1
if ! mountpoint -q /dev/usb-ffs/bulk1; then
    mount -t functionfs bulk1 /dev/usb-ffs/bulk1
fi
echo "[gadget] USB Bulk FunctionFS 已挂载 → /dev/usb-ffs/bulk1"

# ── 9. UDC 绑定由 start.sh 在 psdkd 初始化 FunctionFS ep0 之后完成 ──────────
# FunctionFS 要求用户进程先打开 ep0 并写入 USB 描述符，才能绑定 UDC。
# libpayloadsdk.a 内部负责 ep0 初始化，所以 UDC 绑定在 psdkd 启动后进行。
echo "[gadget] UDC=$UDC (将由 start.sh 绑定)"

echo ""
echo "[gadget] ✓ USB Gadget 结构创建完成 (UDC 待绑定)"
echo "  CDC-ACM 串口: /dev/ttyGS0"
echo "  USB Bulk:     /dev/usb-ffs/bulk1"
echo ""
echo "现在运行:"
echo "  sudo ./tools/start.sh"

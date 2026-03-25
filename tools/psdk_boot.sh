#!/bin/bash
# psdk_boot.sh — PSDK 启动序列
#
# 默认模式: UART (推荐, 避免消耗15秒 USB 枚举窗口)
# 可选模式: USB  (PSDK_LINK_MODE=usb)
#
# 用法:
#   sudo bash tools/psdk_boot.sh
#   sudo PSDK_LINK_MODE=usb bash tools/psdk_boot.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PSDKD="$SCRIPT_DIR/../build/bin/psdkd"
FFS_INIT="$SCRIPT_DIR/ffs_init"
GADGET_DIR=/sys/kernel/config/usb_gadget/psdk
LINK_MODE="${PSDK_LINK_MODE:-uart}"

# ── 0. 检查 root ────────────────────────────────────────────────────────────
if [ "$(id -u)" -ne 0 ]; then
    echo "[ERROR] 需要 root 权限，请用 sudo 运行"
    exit 1
fi

echo "=================================================="
echo " PSDK Boot ($LINK_MODE) — $(date)"
echo "=================================================="

# ── 1. 清理旧进程 ────────────────────────────────────────────────────────────
echo "[1/6] 清理旧进程..."
pkill -f psdkd 2>/dev/null || true
pkill -f ffs_init 2>/dev/null || true
sleep 0.3

if [ ! -x "$PSDKD" ]; then
    echo "[ERROR] 找不到 $PSDKD，请先编译: make PSDK_REAL=1 PLATFORM=orangepi"
    exit 1
fi

if [ "$LINK_MODE" = "uart" ]; then
    echo "[2/6] UART 模式: 跳过 USB Gadget/FunctionFS 初始化"
    echo "[3/6] UART 模式: 跳过 ffs_init"
    echo "[4/6] UART 模式: 跳过 UDC 绑定"
    echo "[5/6] UART 模式: 跳过 ep1/ep2 等待"
    echo "[6/6] 启动 psdkd..."
    exec "$PSDKD" --debug
fi

if [ "$LINK_MODE" != "usb" ]; then
    echo "[ERROR] 不支持的 PSDK_LINK_MODE=$LINK_MODE (可选: uart 或 usb)"
    exit 1
fi

# ── 2. 清理并重建 Gadget ─────────────────────────────────────────────────────
echo "[2/6] 配置 USB Gadget..."

# 解绑UDC
echo "" > "$GADGET_DIR/UDC" 2>/dev/null || true
sleep 0.5

# 清理旧gadget配置
if [ -d "$GADGET_DIR" ]; then
    rm -f "$GADGET_DIR"/configs/c.1/acm.GS0 2>/dev/null || true
    rm -f "$GADGET_DIR"/configs/c.1/ffs.bulk1 2>/dev/null || true
    rmdir "$GADGET_DIR"/functions/acm.GS0 2>/dev/null || true
    rmdir "$GADGET_DIR"/functions/ffs.bulk1 2>/dev/null || true
    rmdir "$GADGET_DIR"/configs/c.1/strings/0x409 2>/dev/null || true
    rmdir "$GADGET_DIR"/configs/c.1 2>/dev/null || true
    rmdir "$GADGET_DIR"/strings/0x409 2>/dev/null || true
    rmdir "$GADGET_DIR" 2>/dev/null || true
    sleep 0.3
fi

# 卸载旧FunctionFS
umount /dev/usb-ffs/bulk1 2>/dev/null || true
sleep 0.2

# 加载内核模块
modprobe libcomposite
modprobe usb_f_acm
modprobe usb_f_fs

# 挂载configfs
if ! mountpoint -q /sys/kernel/config; then
    mount -t configfs none /sys/kernel/config
fi

# 创建gadget (DJI VID/PID)
mkdir -p "$GADGET_DIR"
echo 0x2CA3 > "$GADGET_DIR/idVendor"   # DJI
echo 0x001F > "$GADGET_DIR/idProduct"  # M3E payload
echo 0x0100 > "$GADGET_DIR/bcdDevice"
echo 0x0200 > "$GADGET_DIR/bcdUSB"

mkdir -p "$GADGET_DIR/strings/0x409"
echo "DJI"          > "$GADGET_DIR/strings/0x409/manufacturer"
echo "PSDK Payload" > "$GADGET_DIR/strings/0x409/product"
echo "PSDK0001"     > "$GADGET_DIR/strings/0x409/serialnumber"

mkdir -p "$GADGET_DIR/configs/c.1"
echo 500 > "$GADGET_DIR/configs/c.1/MaxPower"
mkdir -p "$GADGET_DIR/configs/c.1/strings/0x409"
echo "PSDK Config" > "$GADGET_DIR/configs/c.1/strings/0x409/configuration"

# CDC-ACM (UART通道 → /dev/ttyGS0)
mkdir -p "$GADGET_DIR/functions/acm.GS0"
ln -s "$GADGET_DIR/functions/acm.GS0" "$GADGET_DIR/configs/c.1/acm.GS0"

# USB Bulk FunctionFS (数据通道 → /dev/usb-ffs/bulk1)
mkdir -p "$GADGET_DIR/functions/ffs.bulk1"
ln -s "$GADGET_DIR/functions/ffs.bulk1" "$GADGET_DIR/configs/c.1/ffs.bulk1"

# 挂载FunctionFS
mkdir -p /dev/usb-ffs/bulk1
mount -t functionfs bulk1 /dev/usb-ffs/bulk1

echo "[2/6] Gadget 创建完成 (VID=0x2CA3 PID=0x001F)"

# ── 3. 启动 ffs_init ─────────────────────────────────────────────────────────
echo "[3/6] 启动 ffs_init (写入 FunctionFS ep0 描述符)..."

# 编译ffs_init如果不存在
if [ ! -x "$FFS_INIT" ]; then
    gcc -o "$FFS_INIT" "$SCRIPT_DIR/ffs_init.c" || { echo "[ERROR] ffs_init 编译失败"; exit 1; }
fi

"$FFS_INIT" /dev/usb-ffs/bulk1 &
FFS_PID=$!

# 等待ep0初始化完成
sleep 0.5

if ! kill -0 $FFS_PID 2>/dev/null; then
    echo "[ERROR] ffs_init 异常退出"
    exit 1
fi
echo "[3/6] ffs_init 运行中 (PID=$FFS_PID), FunctionFS ACTIVE"

# ── 4. 绑定 UDC ─────────────────────────────────────────────────────────────
UDC=$(ls /sys/class/udc/ | head -1)
echo "[4/6] 绑定 UDC: $UDC"
echo "$UDC" > "$GADGET_DIR/UDC"
sleep 0.5
echo "[4/6] UDC 绑定成功"
echo ""
echo "============================================"
echo "  !! 现在请将 OrangePi USB-C 插入 M3E E-Port !!"
echo "  !! 15秒内必须完成，请迅速操作             !!"
echo "============================================"
echo ""

# ── 5. 等待 ep1/ep2 (M3E枚举) ────────────────────────────────────────────────
echo "[5/6] 等待 M3E 枚举 (最多15秒)..."
for i in $(seq 1 30); do
    if [ -e /dev/usb-ffs/bulk1/ep1 ] && [ -e /dev/usb-ffs/bulk1/ep2 ]; then
        STATE=$(cat /sys/class/udc/$UDC/state 2>/dev/null)
        echo "[5/6] ep1/ep2 出现! state=$STATE (${i}x0.5s后)"
        break
    fi
    sleep 0.5
    if [ $i -eq 30 ]; then
        echo "[WARN] 15秒内未检测到M3E枚举，继续启动psdkd..."
    fi
done

ls /dev/usb-ffs/bulk1/ 2>/dev/null && echo "" || true

# ── 6. 启动 psdkd ─────────────────────────────────────────────────────────────
echo "[6/6] 启动 psdkd..."
exec "$PSDKD" --debug

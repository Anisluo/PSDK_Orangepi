#!/bin/bash
# start.sh — 启动 psdkd (USB Bulk Device 模式)
#
# 前置条件:
#   sudo bash tools/setup_gadget.sh  (创建 gadget 结构，挂载 FunctionFS)
#
# 流程:
#   1. 检查 gadget 已创建
#   2. 启动 ffs_init 写入 ep0 描述符 (lang_count=0 方式，kernel 6.1 兼容)
#   3. 绑定 UDC (ffs_init 保持后台运行处理 ep0 事件)
#   4. 等待 ep1/ep2 出现 (M3E 枚举完成)
#   5. 启动 psdkd

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PSDKD="$SCRIPT_DIR/../build/bin/psdkd"
FFS_INIT="$SCRIPT_DIR/ffs_init"
GADGET_DIR=/sys/kernel/config/usb_gadget/psdk
UDC_FILE="$GADGET_DIR/UDC"

# ── 1. 检查 gadget 结构存在 ────────────────────────────────────────────────
if [ ! -d "$GADGET_DIR" ]; then
    echo "[ERROR] USB Gadget 未创建，请先运行:"
    echo "  sudo bash tools/setup_gadget.sh"
    exit 1
fi

if [ ! -f /dev/usb-ffs/bulk1/ep0 ]; then
    echo "[ERROR] FunctionFS 未挂载，请先运行:"
    echo "  sudo bash tools/setup_gadget.sh"
    exit 1
fi

UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
if [ -z "$UDC" ]; then
    echo "[ERROR] 未找到 UDC 控制器"
    exit 1
fi

# ── 2. 检查 ffs_init 和 psdkd 二进制 ──────────────────────────────────────
if [ ! -x "$FFS_INIT" ]; then
    echo "[start] 编译 ffs_init..."
    gcc -o "$FFS_INIT" "$SCRIPT_DIR/ffs_init.c" || { echo "[ERROR] ffs_init 编译失败"; exit 1; }
fi

if [ ! -x "$PSDKD" ]; then
    echo "[ERROR] 找不到 $PSDKD，请先编译:"
    echo "  make PSDK_REAL=1 PLATFORM=orangepi"
    exit 1
fi

# ── 3. 解绑旧 UDC ─────────────────────────────────────────────────────────
CURRENT_UDC=$(cat "$UDC_FILE" 2>/dev/null || true)
if [ -n "$CURRENT_UDC" ]; then
    echo "[start] 解绑旧 UDC: $CURRENT_UDC"
    echo "" > "$UDC_FILE" || true
    sleep 0.5
fi

# Kill 旧进程
pkill -f ffs_init 2>/dev/null || true
pkill -f psdkd 2>/dev/null || true
sleep 0.3

# ── 4. 启动 ffs_init 写入 ep0 描述符并保持后台运行 ────────────────────────
echo "[start] 启动 ffs_init (写入 FunctionFS ep0 描述符)..."
"$FFS_INIT" /dev/usb-ffs/bulk1 &
FFS_INIT_PID=$!
echo "[start] ffs_init PID=$FFS_INIT_PID"

# 等待 ep0 初始化完成 (ffs_init 写入描述符+strings 约需 <100ms)
sleep 0.5

# ── 5. 绑定 UDC ───────────────────────────────────────────────────────────
echo "[start] 绑定 UDC: $UDC"
if echo "$UDC" > "$UDC_FILE" 2>/dev/null; then
    echo "[start] UDC 绑定成功"
else
    echo "[start] UDC 绑定失败，检查 ffs_init 是否正常运行"
    kill $FFS_INIT_PID 2>/dev/null || true
    exit 1
fi

# ── 6. 等待 ep1/ep2 出现 (M3E 枚举 OrangePi 后出现) ──────────────────────
echo "[start] 等待 M3E 枚举完成 (ep1/ep2 出现)..."
for i in $(seq 1 20); do
    if [ -e /dev/usb-ffs/bulk1/ep1 ] && [ -e /dev/usb-ffs/bulk1/ep2 ]; then
        echo "[start] ep1/ep2 已出现 (${i}00ms)"
        break
    fi
    sleep 0.1
done

ls /dev/usb-ffs/bulk1/ 2>/dev/null || true

# ── 7. 启动 psdkd ─────────────────────────────────────────────────────────
echo "[start] 启动 psdkd..."
echo "[start] OrangePi USB-C 连接到 M3E E-Port"
"$PSDKD" --debug

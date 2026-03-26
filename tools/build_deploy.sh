#!/bin/bash
# build_deploy.sh — 一条命令把当前工程部署到 OrangePi
#
# 用法:
#   ./tools/build_deploy.sh [M3E|M3T|M3TD]
#
# 默认规则:
#   M3E / M3T -> Payload-SDK-3.9.2 + manifold2
#   M3TD      -> Payload-SDK-3.12.0 + manifold3 + dual
#
# 可选环境变量:
#   ORANGEPI_HOST=orangepi@192.168.1.102
#   REMOTE_DIR=/home/orangepi/PSDK
#   SYNC_SDK=1          # 强制同步对应 SDK 到 OrangePi

set -euo pipefail

MODEL="${1:-M3TD}"
ORANGEPI_HOST="${ORANGEPI_HOST:-orangepi@192.168.1.102}"
REMOTE_DIR="${REMOTE_DIR:-/home/orangepi/PSDK}"
REMOTE_DESKTOP="/home/orangepi/Desktop"
SYNC_SDK="${SYNC_SDK:-0}"

usage() {
    cat <<EOF
Usage: ./tools/build_deploy.sh [M3E|M3T|M3TD]

Examples:
  ./tools/build_deploy.sh M3E
  ./tools/build_deploy.sh M3TD
  SYNC_SDK=1 ./tools/build_deploy.sh M3TD
EOF
}

case "$MODEL" in
  M3E|m3e)
    MODEL="M3E"
    SDK_VERSION="3.9.2"
    PLATFORM_VARIANT="manifold2"
    EXTRA_MAKE_ARGS=()
    ;;
  M3T|m3t)
    MODEL="M3T"
    SDK_VERSION="3.9.2"
    PLATFORM_VARIANT="manifold2"
    EXTRA_MAKE_ARGS=()
    ;;
  M3TD|m3td)
    MODEL="M3TD"
    SDK_VERSION="3.12.0"
    PLATFORM_VARIANT="manifold3"
    EXTRA_MAKE_ARGS=("M3TD_CONN_MODE=dual")
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "[ERROR] 不支持的型号: $MODEL" >&2
    usage
    exit 1
    ;;
esac

LOCAL_SDK_DIR="$HOME/Desktop/Payload-SDK-$SDK_VERSION"
REMOTE_SDK_DIR="$REMOTE_DESKTOP/Payload-SDK-$SDK_VERSION"

if [ ! -d "$LOCAL_SDK_DIR" ]; then
    echo "[ERROR] 本地缺少 SDK: $LOCAL_SDK_DIR" >&2
    exit 1
fi

echo "[info] 型号            : $MODEL"
echo "[info] SDK             : Payload-SDK-$SDK_VERSION"
echo "[info] platform        : $PLATFORM_VARIANT"
echo "[info] OrangePi        : $ORANGEPI_HOST"
echo "[info] remote project  : $REMOTE_DIR"
echo "[info] remote sdk      : $REMOTE_SDK_DIR"

echo "[sync] 同步工程源码 -> $ORANGEPI_HOST:$REMOTE_DIR"
rsync -av --delete \
  --exclude='build' \
  --exclude='*.o' \
  --exclude='.git' \
  ./ "$ORANGEPI_HOST:$REMOTE_DIR/"

if [ "$SYNC_SDK" = "1" ] || ! ssh "$ORANGEPI_HOST" "test -d '$REMOTE_SDK_DIR'"; then
    echo "[sync] 同步 SDK -> $ORANGEPI_HOST:$REMOTE_SDK_DIR"
    rsync -av --delete \
      --exclude='build' \
      --exclude='.git' \
      "$LOCAL_SDK_DIR/" "$ORANGEPI_HOST:$REMOTE_SDK_DIR/"
else
    echo "[sync] 远端 SDK 已存在，跳过同步"
fi

echo "[deploy] 写入机型配置: $MODEL"
ssh "$ORANGEPI_HOST" "mkdir -p '$REMOTE_DIR' '$REMOTE_DESKTOP' && echo '$MODEL' > '$REMOTE_DIR/drone_model'"

MAKE_CMD=(
  "cd '$REMOTE_DIR'"
  "&& make clean"
  "&& make -j2"
  "PSDK_REAL=1"
  "PLATFORM=orangepi"
  "DRONE_MODEL=$MODEL"
  "PSDK_PLATFORM_VARIANT=$PLATFORM_VARIANT"
  "PSDK_SDK_DIR=$REMOTE_SDK_DIR"
)
for arg in "${EXTRA_MAKE_ARGS[@]}"; do
  MAKE_CMD+=("$arg")
done

echo "[build] 在 OrangePi 上编译"
ssh "$ORANGEPI_HOST" "${MAKE_CMD[*]}"

echo "[deploy] 重启 psdkd 服务"
ssh "$ORANGEPI_HOST" "sudo systemctl restart psdkd"

echo ""
echo "[OK] 部署完成"
echo "     型号: $MODEL"
echo "     SDK : Payload-SDK-$SDK_VERSION"
echo ""
echo "常用命令:"
echo "  查看服务: ssh $ORANGEPI_HOST 'systemctl status psdkd --no-pager -l'"
echo "  跟日志  : ssh $ORANGEPI_HOST 'journalctl -u psdkd -f'"
echo "  启动日志: ssh $ORANGEPI_HOST 'tail -n 200 /tmp/psdk_boot.log'"

#!/bin/bash
# push_deps_and_deploy_m3e.sh
# 全新 OrangePi 一键部署 (M3E / Payload-SDK-3.9.2)
# 用法: bash tools/push_deps_and_deploy_m3e.sh
#
# 需要: sshpass (sudo apt install sshpass)

set -euo pipefail

HOST="orangepi@192.168.1.103"
PASS="orangepi"
REMOTE_DIR="/home/orangepi/PSDK"
REMOTE_DESKTOP="/home/orangepi/Desktop"
SDK_VERSION="3.9.2"
LOCAL_SDK_DIR="$HOME/Desktop/Payload-SDK-$SDK_VERSION"
REMOTE_SDK_DIR="$REMOTE_DESKTOP/Payload-SDK-$SDK_VERSION"
DEPS_DIR="$HOME"   # .deb 文件位置
SYNC_SDK="${SYNC_SDK:-0}"

SSH="sshpass -p $PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 $HOST"
SCP="sshpass -p $PASS scp -o StrictHostKeyChecking=no"
RSYNC="sshpass -p $PASS rsync -az --info=progress2 -e 'ssh -o StrictHostKeyChecking=no'"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[info]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
error() { echo -e "${RED}[error]${NC} $*" >&2; }

# ── 前置检查 ──────────────────────────────────────────────────────────────────
if ! command -v sshpass &>/dev/null; then
    error "缺少 sshpass，请先运行: sudo apt install sshpass"
    exit 1
fi
if [ ! -d "$LOCAL_SDK_DIR" ]; then
    error "本地缺少 SDK: $LOCAL_SDK_DIR"
    exit 1
fi

# ── Step 1: 连通性 ────────────────────────────────────────────────────────────
info "Step 1/6  测试连通性..."
$SSH "echo ok" >/dev/null || { error "无法连接 $HOST"; exit 1; }
info "连接 OK"

# ── Step 2: 推送 SSH 公钥 ─────────────────────────────────────────────────────
info "Step 2/6  推送 SSH 公钥 (免密登录)..."
PUB_KEY=$(cat ~/.ssh/id_ed25519.pub 2>/dev/null || cat ~/.ssh/id_rsa.pub 2>/dev/null || echo "")
if [ -n "$PUB_KEY" ]; then
    $SSH "mkdir -p ~/.ssh && chmod 700 ~/.ssh && \
          grep -qxF '$PUB_KEY' ~/.ssh/authorized_keys 2>/dev/null || \
          echo '$PUB_KEY' >> ~/.ssh/authorized_keys && \
          chmod 600 ~/.ssh/authorized_keys" && info "公钥已添加" || warn "公钥添加失败，继续用密码"
else
    warn "未找到本地公钥，跳过"
fi

# ── Step 3: 推送 arm64 依赖包并安装 ──────────────────────────────────────────
info "Step 3/6  推送 arm64 依赖包..."
DEBS=(
    "$DEPS_DIR/libjson-c5_0.17-1build1_arm64.deb"
    "$DEPS_DIR/libjson-c-dev_0.17-1build1_arm64.deb"
    "$DEPS_DIR/libssl3t64_3.0.13-0ubuntu3_arm64.deb"
    "$DEPS_DIR/libssl-dev_3.0.13-0ubuntu3_arm64.deb"
    "$DEPS_DIR/libusb-1.0-0_1.0.25-1ubuntu2_arm64.deb"
    "$DEPS_DIR/libusb-1.0-0-dev_1.0.25-1ubuntu2_arm64.deb"
)
MISSING=()
for f in "${DEBS[@]}"; do
    [ -f "$f" ] || MISSING+=("$f")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    error "以下 .deb 文件不存在:"
    for f in "${MISSING[@]}"; do echo "  $f"; done
    exit 1
fi

$SSH "mkdir -p /tmp/psdk_deps"
$SCP "${DEBS[@]}" "$HOST:/tmp/psdk_deps/"
info "安装依赖包..."
$SSH "echo '$PASS' | sudo -S dpkg -i /tmp/psdk_deps/*.deb 2>&1 || echo '$PASS' | sudo -S apt-get install -f -y"
info "依赖安装完成"

# ── Step 4: 同步工程源码 ──────────────────────────────────────────────────────
info "Step 4/6  同步工程源码..."
$SSH "mkdir -p '$REMOTE_DIR' '$REMOTE_DESKTOP'"
eval "$RSYNC" --delete \
    --exclude='build/' --exclude='*.o' --exclude='.git/' \
    ./ "$HOST:$REMOTE_DIR/"

# ── Step 5: 同步 SDK ──────────────────────────────────────────────────────────
if [ "$SYNC_SDK" = "1" ] || ! $SSH "test -d '$REMOTE_SDK_DIR'" 2>/dev/null; then
    info "Step 5/6  同步 Payload-SDK-$SDK_VERSION (较大，请稍候)..."
    eval "$RSYNC" --delete --exclude='.git/' \
        "$LOCAL_SDK_DIR/" "$HOST:$REMOTE_SDK_DIR/"
else
    info "Step 5/6  远端 SDK 已存在，跳过 (SYNC_SDK=1 强制重新同步)"
fi

# ── Step 6: 远端编译 + 自启动 ─────────────────────────────────────────────────
info "Step 6/6  远端编译 + 配置自启动..."
$SSH "echo 'M3E' > '$REMOTE_DIR/drone_model'"
$SSH "cd '$REMOTE_DIR' && \
      make clean && \
      make -j2 \
        PSDK_REAL=1 \
        PLATFORM=orangepi \
        DRONE_MODEL=M3E \
        PSDK_PLATFORM_VARIANT=manifold2 \
        PSDK_SDK_DIR='$REMOTE_SDK_DIR'"
$SSH "cd '$REMOTE_DIR' && echo '$PASS' | sudo -S bash tools/setup_orangepi_autostart.sh"

# ── 完成 ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  部署完成  M3E + Payload-SDK-$SDK_VERSION${NC}"
echo -e "${GREEN}================================================${NC}"
echo "  目标  : $HOST"
echo "  工程  : $REMOTE_DIR"
echo "  SDK   : $REMOTE_SDK_DIR"
echo ""
echo "常用命令:"
echo "  查看服务  : ssh $HOST 'systemctl status psdkd --no-pager -l'"
echo "  实时日志  : ssh $HOST 'journalctl -u psdkd -f'"
echo "  启动日志  : ssh $HOST 'tail -f /tmp/psdk_boot.log'"

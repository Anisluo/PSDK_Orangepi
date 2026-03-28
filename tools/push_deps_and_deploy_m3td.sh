#!/bin/bash
# push_deps_and_deploy_m3td.sh
# 全新 OrangePi 一键部署 (M3TD / Payload-SDK-3.12.0, 离线环境)
# 用法: bash tools/push_deps_and_deploy_m3td.sh
#
# 支持通过环境变量覆盖默认值:
#   HOST=orangepi@192.168.1.104
#   PASS=orangepi
#   DEPS_DIR=$HOME
#   LOCAL_SDK_DIR=$HOME/Desktop/Payload-SDK-3.12.0
#   SYNC_SDK=1

set -euo pipefail

HOST="${HOST:-orangepi@192.168.1.104}"
PASS="${PASS:-orangepi}"
REMOTE_DIR="${REMOTE_DIR:-/home/orangepi/PSDK}"
REMOTE_DESKTOP="${REMOTE_DESKTOP:-/home/orangepi/Desktop}"
SDK_VERSION="${SDK_VERSION:-3.12.0}"
LOCAL_SDK_DIR="${LOCAL_SDK_DIR:-$HOME/Desktop/Payload-SDK-$SDK_VERSION}"
REMOTE_SDK_DIR="${REMOTE_SDK_DIR:-$REMOTE_DESKTOP/Payload-SDK-$SDK_VERSION}"
DEPS_DIR="${DEPS_DIR:-$HOME}"
SYNC_SDK="${SYNC_SDK:-0}"
JOBS="${JOBS:-2}"

SSH="sshpass -p $PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 $HOST"
SCP="sshpass -p $PASS scp -o StrictHostKeyChecking=no"
RSYNC="sshpass -p $PASS rsync -az --info=progress2 -e 'ssh -o StrictHostKeyChecking=no'"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[info]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
error() { echo -e "${RED}[error]${NC} $*" >&2; }

DEBS=()

add_deb() {
    local label=$1
    shift
    local found=""
    local pattern

    shopt -s nullglob
    for pattern in "$@"; do
        for found in "$DEPS_DIR"/$pattern; do
            DEBS+=("$found")
            shopt -u nullglob
            return 0
        done
    done
    shopt -u nullglob

    error "在 $DEPS_DIR 中找不到依赖包: $label"
    error "尝试过的模式: $*"
    exit 1
}

# ── 前置检查 ──────────────────────────────────────────────────────────────────
if ! command -v sshpass &>/dev/null; then
    error "缺少 sshpass，请先运行: sudo apt install sshpass"
    exit 1
fi

if [ ! -d "$LOCAL_SDK_DIR" ]; then
    error "本地缺少 SDK: $LOCAL_SDK_DIR"
    exit 1
fi

add_deb "libjson-c5" "libjson-c5_*_arm64.deb"
add_deb "libjson-c-dev" "libjson-c-dev_*_arm64.deb"
add_deb "libssl3 / libssl3t64" "libssl3_*_arm64.deb" "libssl3t64_*_arm64.deb"
add_deb "libssl-dev" "libssl-dev_*_arm64.deb"
add_deb "libusb-1.0-0" "libusb-1.0-0_*_arm64.deb"
add_deb "libusb-1.0-0-dev" "libusb-1.0-0-dev_*_arm64.deb"

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

# ── Step 3: 推送 arm64 依赖包并离线安装 ──────────────────────────────────────
info "Step 3/6  推送 arm64 依赖包..."
$SSH "rm -rf /tmp/psdk_deps && mkdir -p /tmp/psdk_deps"
$SCP "${DEBS[@]}" "$HOST:/tmp/psdk_deps/"

info "离线安装依赖包..."
if ! $SSH "echo '$PASS' | sudo -S dpkg -i /tmp/psdk_deps/*.deb"; then
    error "远端 dpkg -i 安装失败。目标机未联外网，请确认缺失依赖对应的 arm64 .deb 也已放在 $DEPS_DIR。"
    exit 1
fi
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
$SSH "printf '%s\n' 'M3TD' > '$REMOTE_DIR/drone_model'"
$SSH "cd '$REMOTE_DIR' && \
      make clean && \
      make -j$JOBS \
        PSDK_REAL=1 \
        PLATFORM=orangepi \
        DRONE_MODEL=M3TD \
        PSDK_PLATFORM_VARIANT=manifold3 \
        M3TD_CONN_MODE=dual \
        PSDK_SDK_DIR='$REMOTE_SDK_DIR'"
$SSH "cd '$REMOTE_DIR' && echo '$PASS' | sudo -S bash tools/setup_orangepi_autostart.sh"

# ── 完成 ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  部署完成  M3TD + Payload-SDK-$SDK_VERSION${NC}"
echo -e "${GREEN}================================================${NC}"
echo "  目标  : $HOST"
echo "  工程  : $REMOTE_DIR"
echo "  SDK   : $REMOTE_SDK_DIR"
echo "  型号  : M3TD"
echo ""
echo "常用命令:"
echo "  查看服务  : ssh $HOST 'systemctl status psdkd --no-pager -l'"
echo "  实时日志  : ssh $HOST 'journalctl -u psdkd -f'"
echo "  启动日志  : ssh $HOST 'tail -f /tmp/psdk_boot.log'"
echo "  尝试日志  : ssh $HOST 'tail -f /tmp/psdk_attempt.log'"

#!/bin/bash
# fetch_deps.sh — Download arm64 Ubuntu Noble packages on x86 dev machine,
#                 then SCP them to OrangePi and install.
#
# Usage:
#   bash tools/fetch_deps.sh
#
# Requirements on dev machine: apt-transport-https, wget, sshpass (or ssh key)

set -e

ORANGEPI_HOST="orangepi@192.168.1.102"
REMOTE_DIR="/tmp/psdk_deps"
LOCAL_DIR="/tmp/psdk_deps_arm64"
NOBLE_MIRROR="http://ports.ubuntu.com/ubuntu-ports"

echo "=== Step 1: Create temporary Noble arm64 apt sources ==="
mkdir -p "$LOCAL_DIR"

SOURCES_FILE="$(mktemp /tmp/noble-arm64.XXXX.list)"
cat > "$SOURCES_FILE" <<EOF
deb [arch=arm64] $NOBLE_MIRROR noble main restricted universe multiverse
deb [arch=arm64] $NOBLE_MIRROR noble-updates main restricted universe multiverse
deb [arch=arm64] $NOBLE_MIRROR noble-security main restricted universe multiverse
EOF

echo "=== Step 2: Update package list for Noble arm64 ==="
sudo apt-get update \
    -o Dir::Etc::sourcelist="$SOURCES_FILE" \
    -o Dir::Etc::sourcelistd="/dev/null" \
    -o APT::Architecture="arm64" \
    -o Dir::State::Lists="$LOCAL_DIR/apt-lists" \
    2>&1 | tail -5

echo "=== Step 3: Download arm64 packages ==="
cd "$LOCAL_DIR"

# Download each package with all dependencies
PACKAGES=(
    "libjson-c5:arm64"
    "libjson-c-dev:arm64"
    "libusb-1.0-0:arm64"
    "libusb-1.0-0-dev:arm64"
    "libssl3t64:arm64"
    "libssl-dev:arm64"
    "libgcc-s1:arm64"
    "gcc:arm64"
    "make"
    "gcc-aarch64-linux-gnu"
)

for pkg in "${PACKAGES[@]}"; do
    echo "  Downloading $pkg ..."
    apt-get download \
        -o Dir::Etc::sourcelist="$SOURCES_FILE" \
        -o Dir::Etc::sourcelistd="/dev/null" \
        -o APT::Architecture="arm64" \
        -o Dir::State::Lists="$LOCAL_DIR/apt-lists" \
        "$pkg" 2>/dev/null || echo "  WARNING: $pkg not found, skipping"
done

# Also grab runtime deps that may be missing
RUNTIME_PKGS=(
    "libgcc-s1:arm64"
    "libc6:arm64"
    "zlib1g:arm64"
    "libudev1:arm64"
)
for pkg in "${RUNTIME_PKGS[@]}"; do
    apt-get download \
        -o Dir::Etc::sourcelist="$SOURCES_FILE" \
        -o Dir::Etc::sourcelistd="/dev/null" \
        -o APT::Architecture="arm64" \
        -o Dir::State::Lists="$LOCAL_DIR/apt-lists" \
        "$pkg" 2>/dev/null || true
done

echo ""
echo "=== Step 4: Downloaded packages ==="
ls -lh "$LOCAL_DIR"/*.deb 2>/dev/null || echo "No .deb files found — check errors above"

rm -f "$SOURCES_FILE"

echo ""
echo "=== Step 5: Transfer to OrangePi ($ORANGEPI_HOST) ==="
ssh "$ORANGEPI_HOST" "mkdir -p $REMOTE_DIR"
scp "$LOCAL_DIR"/*.deb "$ORANGEPI_HOST:$REMOTE_DIR/"

echo ""
echo "=== Step 6: Install on OrangePi ==="
ssh "$ORANGEPI_HOST" "sudo dpkg -i $REMOTE_DIR/*.deb 2>&1 || sudo apt-get install -f -y"

echo ""
echo "=== Step 7: Verify on OrangePi ==="
ssh "$ORANGEPI_HOST" "pkg-config --libs json-c libusb-1.0 2>/dev/null && echo 'pkg-config OK' || echo 'pkg-config missing (may still work)'"
ssh "$ORANGEPI_HOST" "ldconfig -p | grep -E 'json|usb|ssl' | head -10"

echo ""
echo "Done. Now transfer PSDK source and build on OrangePi:"
echo "  rsync -av ~/Desktop/PSDK/ orangepi@192.168.1.102:~/PSDK/"
echo "  rsync -av ~/Desktop/Payload-SDK-3.9.2/ orangepi@192.168.1.102:~/Desktop/Payload-SDK-3.9.2/"
echo "  ssh orangepi@192.168.1.102 'cd ~/PSDK && make PSDK_REAL=1 PLATFORM=orangepi'"

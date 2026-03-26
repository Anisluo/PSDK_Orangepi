#!/bin/bash
# start_dual.sh -- PSDK dual transport startup for OrangePi Zero3
# 支持 M3E/M3T (RNDIS + ACM) 和 M3TD (USB Bulk FunctionFS + ACM)
set -euo pipefail

PSDK_DIR=/home/orangepi/PSDK
PSDK_BIN="$PSDK_DIR/build/bin/psdkd"
GADGET=/sys/kernel/config/usb_gadget/psdk
UDC_FILE=$GADGET/UDC
UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
BOOT_LOG=/tmp/psdk_boot.log
ATTEMPT_LOG=/tmp/psdk_attempt.log
NEGOTIATE_TIMEOUT=180
START_DELAYS="0 2 4"
CYCLE_SLEEP=4
USB_WAIT_POLLS=40
M3TD_USB_PROFILE="${M3TD_USB_PROFILE:-}"
M3TD_ENUM_SETTLE="${M3TD_ENUM_SETTLE:-2}"
M3TD_PSDK_START_DELAY="${M3TD_PSDK_START_DELAY:-2}"

# 读取型号配置: M3E / M3T → RNDIS 模式; M3TD → USB Bulk FunctionFS 模式 [默认]
MODEL_FILE="$PSDK_DIR/drone_model"
DRONE_MODEL="M3TD"
if [ -f "$MODEL_FILE" ]; then
    DRONE_MODEL=$(cat "$MODEL_FILE" | tr -d '[:space:]')
fi

if [ -z "$M3TD_USB_PROFILE" ]; then
    if [ "$DRONE_MODEL" = "M3TD" ]; then
        M3TD_USB_PROFILE="manifold3"
    else
        M3TD_USB_PROFILE="legacy"
    fi
fi

CARRIER_WAIT_POLLS=6

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: please run with sudo/root"
    exit 1
fi

if [ -z "${UDC:-}" ]; then
    echo "ERROR: no UDC found under /sys/class/udc"
    exit 1
fi

if [ ! -x "$PSDK_BIN" ]; then
    echo "ERROR: missing binary: $PSDK_BIN"
    exit 1
fi

timestamp() {
    date '+%F %T'
}

log() {
    echo "[$(timestamp)] $*"
}

ensure_configfs() {
    if ! mountpoint -q /sys/kernel/config; then
        mount -t configfs none /sys/kernel/config
    fi
}

gen_mac_suffix() {
    local seed
    if [ -r /etc/machine-id ]; then
        seed=$(tr -d '\n' </etc/machine-id | tail -c 12)
    else
        seed="024680135790"
    fi
    echo "$seed"
}

setup_rndis_macs() {
    local seed b1 b2 b3 b4 b5 b6_host b6_dev func
    seed=$(gen_mac_suffix)
    b1=$(printf "%02x" $(( (16#${seed:0:2} & 0xfe) | 0x02 )))
    b2=${seed:2:2}
    b3=${seed:4:2}
    b4=${seed:6:2}
    b5=${seed:8:2}
    b6_host=$(printf "%02x" $(( (16#${seed:10:2} & 0xfc) | 0x00 )))
    b6_dev=$(printf "%02x" $(( (16#${seed:10:2} & 0xfc) | 0x01 )))
    func="$GADGET/functions/rndis.usb0"
    echo "${b1}:${b2}:${b3}:${b4}:${b5}:${b6_host}" > "$func/host_addr"
    echo "${b1}:${b2}:${b3}:${b4}:${b5}:${b6_dev}" > "$func/dev_addr"
}

cleanup_processes() {
    log "[1] cleaning previous processes"
    # SIGTERM 先，然后 SIGKILL 确保清理（psdkd 可能阻塞在 SDK 调用里无法响应 SIGTERM）
    pkill -x psdkd 2>/dev/null || true
    pkill -f ffs_init 2>/dev/null || true
    sleep 1
    pkill -9 -x psdkd 2>/dev/null || true
    pkill -9 -f ffs_init 2>/dev/null || true
    sleep 0.5
}

reset_usb_net() {
    ip link set usb0 down 2>/dev/null || true
    ip addr flush dev usb0 2>/dev/null || true
}

cleanup_ffs_mounts() {
    local dir
    for dir in bulk1 bulk2 bulk3 bulk6; do
        if mountpoint -q "/dev/usb-ffs/$dir" 2>/dev/null; then
            umount "/dev/usb-ffs/$dir" 2>/dev/null || true
        fi
    done
}

mount_ffs_dir() {
    local name=$1
    mkdir -p "/dev/usb-ffs/$name"
    if ! mountpoint -q "/dev/usb-ffs/$name"; then
        mount -t functionfs "$name" "/dev/usb-ffs/$name"
    fi
}

rebuild_gadget() {
    log "[2] rebuilding USB gadget (model=$DRONE_MODEL)"

    reset_usb_net
    ensure_configfs

    if [ -e "$UDC_FILE" ]; then
        echo "" > "$UDC_FILE" 2>/dev/null || true
        sleep 0.5
    fi

    modprobe libcomposite 2>/dev/null || true
    modprobe usb_f_acm 2>/dev/null || true

    cleanup_ffs_mounts

    if [ -d "$GADGET" ]; then
        rm -f "$GADGET"/configs/c.1/rndis.usb0 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/acm.GS0 2>/dev/null || true
        rm -f "$GADGET"/os_desc/c.1 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/ffs.bulk1 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/ffs.bulk2 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/ffs.bulk3 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/ffs.bulk6 2>/dev/null || true
        rmdir "$GADGET"/functions/rndis.usb0 2>/dev/null || true
        rmdir "$GADGET"/functions/ffs.bulk1 2>/dev/null || true
        rmdir "$GADGET"/functions/ffs.bulk2 2>/dev/null || true
        rmdir "$GADGET"/functions/ffs.bulk3 2>/dev/null || true
        rmdir "$GADGET"/functions/ffs.bulk6 2>/dev/null || true
        rmdir "$GADGET"/functions/acm.GS0 2>/dev/null || true
        rmdir "$GADGET"/os_desc 2>/dev/null || true
        rmdir "$GADGET"/configs/c.1/strings/0x409 2>/dev/null || true
        rmdir "$GADGET"/configs/c.1 2>/dev/null || true
        rmdir "$GADGET"/strings/0x409 2>/dev/null || true
        rmdir "$GADGET" 2>/dev/null || true
    fi

    if [ "$DRONE_MODEL" = "M3TD" ]; then
        # M3TD: USB Bulk FunctionFS.
        modprobe usb_f_fs 2>/dev/null || true

        mkdir -p "$GADGET"
        echo 0x2CA3 > "$GADGET"/idVendor
        if [ "$M3TD_USB_PROFILE" = "manifold3" ]; then
            echo 0x3181 > "$GADGET"/idProduct
        else
            echo 0x001F > "$GADGET"/idProduct
        fi
        echo 0x0100 > "$GADGET"/bcdDevice
        echo 0x0200 > "$GADGET"/bcdUSB
        mkdir -p "$GADGET"/strings/0x409
        echo "DJI"          > "$GADGET"/strings/0x409/manufacturer
        echo "PSDK Payload" > "$GADGET"/strings/0x409/product
        echo "PSDK0001"     > "$GADGET"/strings/0x409/serialnumber
        mkdir -p "$GADGET"/configs/c.1/strings/0x409
        echo 500  > "$GADGET"/configs/c.1/MaxPower
        echo "PSDK" > "$GADGET"/configs/c.1/strings/0x409/configuration

        if [ "$M3TD_USB_PROFILE" = "manifold3" ]; then
            mkdir -p "$GADGET"/functions/ffs.bulk2
            mkdir -p "$GADGET"/functions/ffs.bulk3
            mkdir -p "$GADGET"/functions/ffs.bulk6
            ln -sfn "$GADGET"/functions/ffs.bulk2 "$GADGET"/configs/c.1/ffs.bulk2
            ln -sfn "$GADGET"/functions/ffs.bulk3 "$GADGET"/configs/c.1/ffs.bulk3
            ln -sfn "$GADGET"/functions/ffs.bulk6 "$GADGET"/configs/c.1/ffs.bulk6
            mount_ffs_dir bulk2
            mount_ffs_dir bulk3
            mount_ffs_dir bulk6
            log "USB Bulk FunctionFS mounted → /dev/usb-ffs/{bulk2,bulk3,bulk6}"
        else
            mkdir -p "$GADGET"/functions/ffs.bulk1
            ln -sfn "$GADGET"/functions/ffs.bulk1 "$GADGET"/configs/c.1/ffs.bulk1
            mount_ffs_dir bulk1
            log "USB Bulk FunctionFS mounted → /dev/usb-ffs/bulk1"
        fi
    else
        # M3E / M3T: RNDIS + CDC-ACM (Manifold2 VID/PID)
        modprobe usb_f_rndis 2>/dev/null || true

        mkdir -p "$GADGET"
        echo 0x0955 > "$GADGET"/idVendor
        echo 0x7020 > "$GADGET"/idProduct
        echo 0x0002 > "$GADGET"/bcdDevice
        echo 0x0200 > "$GADGET"/bcdUSB
        echo 0xEF > "$GADGET"/bDeviceClass
        echo 0x02 > "$GADGET"/bDeviceSubClass
        echo 0x01 > "$GADGET"/bDeviceProtocol
        mkdir -p "$GADGET"/strings/0x409
        echo "NVIDIA"       > "$GADGET"/strings/0x409/manufacturer
        echo "Manifold2"    > "$GADGET"/strings/0x409/product
        echo "DJI00000001"  > "$GADGET"/strings/0x409/serialnumber
        mkdir -p "$GADGET"/os_desc
        echo 1        > "$GADGET"/os_desc/use
        echo 0xcd     > "$GADGET"/os_desc/b_vendor_code
        echo "MSFT100" > "$GADGET"/os_desc/qw_sign
        mkdir -p "$GADGET"/configs/c.1/strings/0x409
        echo 250   > "$GADGET"/configs/c.1/MaxPower
        echo "PSDK" > "$GADGET"/configs/c.1/strings/0x409/configuration

        mkdir -p "$GADGET"/functions/rndis.usb0
        setup_rndis_macs
        echo "RNDIS"   > "$GADGET"/functions/rndis.usb0/os_desc/interface.rndis/compatible_id
        echo "5162001" > "$GADGET"/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id
        ln -s "$GADGET"/functions/rndis.usb0 "$GADGET"/configs/c.1/
        mkdir -p "$GADGET"/functions/acm.GS0
        ln -s "$GADGET"/functions/acm.GS0 "$GADGET"/configs/c.1/
        ln -s "$GADGET"/configs/c.1 "$GADGET"/os_desc/c.1
    fi

    if [ "$DRONE_MODEL" != "M3TD" ]; then
        log "[3] binding UDC: $UDC"
        echo "$UDC" > "$UDC_FILE"
        log "UDC state: $(cat /sys/class/udc/"$UDC"/state 2>/dev/null || echo unknown)"
    else
        log "[3] M3TD: UDC binding deferred until after ffs_init ($M3TD_USB_PROFILE)"
    fi
}

wait_for_usb0() {
    local i

    if [ "$DRONE_MODEL" = "M3TD" ]; then
        # M3TD: FunctionFS 由 psdkd 内部初始化 ep0，不需要等 usb0
        log "M3TD: skipping usb0 wait (USB Bulk mode)"
        return 0
    fi

    log "waiting for usb0 interface (up to $((USB_WAIT_POLLS / 2))s)"
    for i in $(seq 1 "$USB_WAIT_POLLS"); do
        if ip link show usb0 >/dev/null 2>&1; then
            log "usb0 detected on poll $i/$USB_WAIT_POLLS"
            return 0
        fi
        sleep 0.5
    done

    log "usb0 did not appear"
    return 1
}

wait_for_carrier() {
    local i

    log "checking usb0 carrier in background (up to $((CARRIER_WAIT_POLLS / 2))s)"
    for i in $(seq 1 "$CARRIER_WAIT_POLLS"); do
        if [ "$(cat /sys/class/net/usb0/carrier 2>/dev/null || echo 0)" = "1" ]; then
            log "usb0 carrier detected on poll $i/$CARRIER_WAIT_POLLS"
            return 0
        fi
        log "usb0 exists but carrier is not ready yet ($i/$CARRIER_WAIT_POLLS)"
        sleep 0.5
    done

    log "usb0 carrier did not appear yet"
    return 1
}

setup_m3td_udc() {
    local FFS_INIT="$PSDK_DIR/tools/ffs_init"
    local FFS_PATH=/dev/usb-ffs/bulk1

    # 编译 ffs_init（源码比二进制新时重新编译）
    local FFS_SRC="$PSDK_DIR/tools/ffs_init.c"
    if [ ! -x "$FFS_INIT" ] || [ "$FFS_SRC" -nt "$FFS_INIT" ]; then
        log "[3] 编译 ffs_init..."
        gcc -o "$FFS_INIT" "$FFS_SRC" || {
            log "ffs_init 编译失败"
            return 1
        }
    fi

    # 启动 ffs_init 写入 ep0 描述符并后台保持运行
    pkill -f ffs_init 2>/dev/null || true
    if [ "$M3TD_USB_PROFILE" = "manifold3" ]; then
        "$FFS_INIT" /dev/usb-ffs/bulk2 2 0x83 0x02 &
        "$FFS_INIT" /dev/usb-ffs/bulk3 3 0x84 0x03 &
        "$FFS_INIT" /dev/usb-ffs/bulk6 6 0x87 0x06 &
    else
        "$FFS_INIT" "$FFS_PATH" &
    fi
    sleep 0.5

    # 绑定 UDC
    log "[3] binding UDC: $UDC (M3TD)"
    if echo "$UDC" > "$UDC_FILE" 2>/dev/null; then
        log "UDC 绑定成功"
    else
        log "UDC 绑定失败"
        return 1
    fi
    log "UDC state: $(cat /sys/class/udc/"$UDC"/state 2>/dev/null || echo unknown)"

    # 等待 ep1/ep2 出现（无人机枚举完成）
    log "[4] 等待 M3TD 枚举完成 (profile=$M3TD_USB_PROFILE)..."
    local i
    for i in $(seq 1 40); do
        if [ "$M3TD_USB_PROFILE" = "manifold3" ]; then
            if [ -e /dev/usb-ffs/bulk2/ep1 ] && [ -e /dev/usb-ffs/bulk2/ep2 ] && \
               [ -e /dev/usb-ffs/bulk3/ep1 ] && [ -e /dev/usb-ffs/bulk3/ep2 ] && \
               [ -e /dev/usb-ffs/bulk6/ep1 ] && [ -e /dev/usb-ffs/bulk6/ep2 ]; then
                log "bulk2/bulk3/bulk6 ep1/ep2 已出现 (${i}×0.5s)"
                if [ "$M3TD_USB_PROFILE" = "manifold3" ] && [ "${M3TD_ENUM_SETTLE:-0}" -gt 0 ]; then
                    log "[4] waiting ${M3TD_ENUM_SETTLE}s for manifold3 link settle"
                    sleep "$M3TD_ENUM_SETTLE"
                fi
                return 0
            fi
        else
            if [ -e "$FFS_PATH/ep1" ] && [ -e "$FFS_PATH/ep2" ]; then
                log "ep1/ep2 已出现 (${i}×0.5s)"
                return 0
            fi
        fi
        sleep 0.5
    done
    log "M3TD endpoints 未出现，继续尝试..."
    return 0
}

prepare_usb0() {
    if [ "$DRONE_MODEL" = "M3TD" ]; then
        log "[4] M3TD: no usb0 setup needed (USB Bulk mode)"
        return 0
    fi
    ip link set usb0 up 2>/dev/null || true
    ip addr flush dev usb0 2>/dev/null || true
    ip addr add 192.168.5.3/24 dev usb0 2>/dev/null || true
    log "[4] usb0 prepared"
    ip addr show usb0 | sed 's/^/    /'
}

start_and_watch() {
    local pid
    local elapsed
    local start_delay=$1

    if [ "$DRONE_MODEL" = "M3TD" ] && [ "$M3TD_USB_PROFILE" = "manifold3" ] && \
       [ "$start_delay" -lt "$M3TD_PSDK_START_DELAY" ]; then
        start_delay=$M3TD_PSDK_START_DELAY
    fi

    : > "$ATTEMPT_LOG"
    if [ "$start_delay" -gt 0 ]; then
        log "[5] delaying psdkd start by ${start_delay}s"
        sleep "$start_delay"
    fi
    stdbuf -oL -eL "$PSDK_BIN" --debug >>"$ATTEMPT_LOG" 2>&1 &
    pid=$!
    log "[5] started psdkd pid=$pid"

    for elapsed in $(seq 1 "$NEGOTIATE_TIMEOUT"); do
        if ! kill -0 "$pid" 2>/dev/null; then
            log "psdkd exited before startup completed"
            tail -n 80 "$ATTEMPT_LOG" | sed 's/^/    /'
            return 1
        fi

        if grep -q "DjiCore_Init failed" "$ATTEMPT_LOG"; then
            log "startup failed during DjiCore_Init"
            tail -n 80 "$ATTEMPT_LOG" | sed 's/^/    /'
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            return 1
        fi

        if grep -q "listening on UDP port" "$ATTEMPT_LOG"; then
            log "startup succeeded"
            cat "$ATTEMPT_LOG"
            wait "$pid"
            return $?
        fi

        sleep 1
    done

    log "startup timed out after ${NEGOTIATE_TIMEOUT}s"
    tail -n 80 "$ATTEMPT_LOG" | sed 's/^/    /'
    kill "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    return 1
}

main() {
    local cycle
    local start_delay
    local effective_start_delays="$START_DELAYS"

    : > "$BOOT_LOG"
    log "=== PSDK startup (model=$DRONE_MODEL) ===" | tee -a "$BOOT_LOG"

    if [ "$DRONE_MODEL" = "M3TD" ] && [ "$M3TD_USB_PROFILE" = "manifold3" ]; then
        effective_start_delays="2 4"
        log "manifold3 startup profile enabled; using start delays: $effective_start_delays" | tee -a "$BOOT_LOG"
    fi

    cycle=0
    while true; do
        cycle=$((cycle + 1))
        log "===== startup cycle $cycle =====" | tee -a "$BOOT_LOG"

        # 每个 cycle 开始时完整重建 gadget（首次或上一 cycle 彻底失败后）
        cleanup_processes
        rebuild_gadget

        if [ "$DRONE_MODEL" = "M3TD" ]; then
            if ! setup_m3td_udc; then
                log "cycle $cycle failed at M3TD UDC setup" | tee -a "$BOOT_LOG"
                sleep "$CYCLE_SLEEP"
                continue
            fi
        else
            if ! wait_for_usb0; then
                log "cycle $cycle failed before psdkd start (usb0 missing)" | tee -a "$BOOT_LOG"
                reset_usb_net
                sleep "$CYCLE_SLEEP"
                continue
            fi
            prepare_usb0
            wait_for_carrier >>"$BOOT_LOG" 2>&1 &
        fi

        # inner retry: 只重启 psdkd，不重建 gadget，保持 E-Port 通信不中断
        for start_delay in $effective_start_delays; do
            log "----- cycle $cycle delay=${start_delay}s -----" | tee -a "$BOOT_LOG"
            cleanup_processes

            if start_and_watch "$start_delay" | tee -a "$BOOT_LOG"; then
                return 0
            fi

            log "cycle $cycle delay=${start_delay}s failed; retrying psdkd without gadget rebuild" | tee -a "$BOOT_LOG"
            sleep 1
        done

        log "cycle $cycle exhausted; rebuilding gadget and sleeping ${CYCLE_SLEEP}s" | tee -a "$BOOT_LOG"
        reset_usb_net
        sleep "$CYCLE_SLEEP"
    done
}

main "$@"

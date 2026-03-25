#!/bin/bash
# start_dual.sh -- PSDK dual transport startup for OrangePi Zero3 + M3E E-Port
set -euo pipefail

PSDK_DIR=/home/orangepi/PSDK
PSDK_BIN="$PSDK_DIR/build/bin/psdkd"
GADGET=/sys/kernel/config/usb_gadget/psdk
UDC_FILE=$GADGET/UDC
UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
BOOT_LOG=/tmp/psdk_boot.log
ATTEMPT_LOG=/tmp/psdk_attempt.log
NEGOTIATE_TIMEOUT=24
START_DELAYS="0 2 4"
CYCLE_SLEEP=4
USB_WAIT_POLLS=40
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
    pkill -x psdkd 2>/dev/null || true
    pkill -f '/home/orangepi/PSDK/build/bin/psdkd --debug' 2>/dev/null || true
    pkill -f ffs_init 2>/dev/null || true
    sleep 0.5
}

reset_usb_net() {
    ip link set usb0 down 2>/dev/null || true
    ip addr flush dev usb0 2>/dev/null || true
}

rebuild_gadget() {
    log "[2] rebuilding USB gadget"

    reset_usb_net
    ensure_configfs

    if [ -e "$UDC_FILE" ]; then
        echo "" > "$UDC_FILE" 2>/dev/null || true
        sleep 0.5
    fi

    modprobe libcomposite 2>/dev/null || true
    modprobe usb_f_rndis 2>/dev/null || true
    modprobe usb_f_acm 2>/dev/null || true

    if [ -d "$GADGET" ]; then
        rm -f "$GADGET"/configs/c.1/rndis.usb0 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/acm.GS0 2>/dev/null || true
        rm -f "$GADGET"/os_desc/c.1 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/ffs.bulk1 2>/dev/null || true
        rm -f "$GADGET"/configs/c.1/acm.GS0 2>/dev/null || true
        rmdir "$GADGET"/functions/rndis.usb0 2>/dev/null || true
        rmdir "$GADGET"/functions/ffs.bulk1 2>/dev/null || true
        rmdir "$GADGET"/functions/acm.GS0 2>/dev/null || true
        rmdir "$GADGET"/os_desc 2>/dev/null || true
        rmdir "$GADGET"/configs/c.1/strings/0x409 2>/dev/null || true
        rmdir "$GADGET"/configs/c.1 2>/dev/null || true
        rmdir "$GADGET"/strings/0x409 2>/dev/null || true
        rmdir "$GADGET" 2>/dev/null || true
    fi

    mkdir -p "$GADGET"
    echo 0x0955 > "$GADGET"/idVendor
    echo 0x7020 > "$GADGET"/idProduct
    echo 0x0002 > "$GADGET"/bcdDevice
    echo 0x0200 > "$GADGET"/bcdUSB
    echo 0xEF > "$GADGET"/bDeviceClass
    echo 0x02 > "$GADGET"/bDeviceSubClass
    echo 0x01 > "$GADGET"/bDeviceProtocol
    mkdir -p "$GADGET"/strings/0x409
    echo "NVIDIA" > "$GADGET"/strings/0x409/manufacturer
    echo "Manifold2" > "$GADGET"/strings/0x409/product
    echo "DJI00000001" > "$GADGET"/strings/0x409/serialnumber
    mkdir -p "$GADGET"/os_desc
    echo 1 > "$GADGET"/os_desc/use
    echo 0xcd > "$GADGET"/os_desc/b_vendor_code
    echo "MSFT100" > "$GADGET"/os_desc/qw_sign
    mkdir -p "$GADGET"/configs/c.1/strings/0x409
    echo 250 > "$GADGET"/configs/c.1/MaxPower
    echo "PSDK" > "$GADGET"/configs/c.1/strings/0x409/configuration
    mkdir -p "$GADGET"/functions/rndis.usb0
    setup_rndis_macs
    echo "RNDIS" > "$GADGET"/functions/rndis.usb0/os_desc/interface.rndis/compatible_id
    echo "5162001" > "$GADGET"/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id
    ln -s "$GADGET"/functions/rndis.usb0 "$GADGET"/configs/c.1/
    mkdir -p "$GADGET"/functions/acm.GS0
    ln -s "$GADGET"/functions/acm.GS0 "$GADGET"/configs/c.1/
    ln -s "$GADGET"/configs/c.1 "$GADGET"/os_desc/c.1

    log "[3] binding UDC: $UDC"
    echo "$UDC" > "$UDC_FILE"
    log "UDC state: $(cat /sys/class/udc/"$UDC"/state 2>/dev/null || echo unknown)"
}

wait_for_usb0() {
    local i

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

prepare_usb0() {
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
    wait "$pid" 2>/dev/null || true
    return 1
}

main() {
    local cycle
    local start_delay

    : > "$BOOT_LOG"
    log "=== PSDK startup (UART=/dev/ttyUSB0 + USB RNDIS) ===" | tee -a "$BOOT_LOG"

    cycle=0
    while true; do
        cycle=$((cycle + 1))
        log "===== startup cycle $cycle =====" | tee -a "$BOOT_LOG"
        for start_delay in $START_DELAYS; do
            log "----- cycle $cycle delay=${start_delay}s -----" | tee -a "$BOOT_LOG"
            cleanup_processes
            rebuild_gadget

            if ! wait_for_usb0; then
                log "cycle $cycle delay=${start_delay}s failed before psdkd start" | tee -a "$BOOT_LOG"
                continue
            fi

            prepare_usb0
            wait_for_carrier >>"$BOOT_LOG" 2>&1 &

            if start_and_watch "$start_delay" | tee -a "$BOOT_LOG"; then
                return 0
            fi

            log "cycle $cycle delay=${start_delay}s failed; resetting before retry" | tee -a "$BOOT_LOG"
            reset_usb_net
            sleep 1
        done

        log "cycle $cycle exhausted; sleeping ${CYCLE_SLEEP}s before next cycle" | tee -a "$BOOT_LOG"
        sleep "$CYCLE_SLEEP"
    done
}

main "$@"

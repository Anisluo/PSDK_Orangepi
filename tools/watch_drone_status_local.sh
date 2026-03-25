#!/bin/bash
set -euo pipefail

REMOTE_USER="${REMOTE_USER:-orangepi}"
REMOTE_HOST="${REMOTE_HOST:-192.168.1.102}"
REMOTE_PORT="${REMOTE_PORT:-22}"
INTERVAL="${INTERVAL:-5}"

print_header() {
    cat <<EOF
Remote drone status watcher
  target  : ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PORT}
  interval: ${INTERVAL}s

Press Ctrl+C to stop.
EOF
}

fetch_diag() {
    ssh -o StrictHostKeyChecking=no -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOST}" 'bash -s' <<'EOF'
echo "---- remote diag ----"
date -u '+utc: %F %T'
echo "[systemd]"
sudo systemctl status psdkd.service --no-pager -l | tail -n 40 || true
echo "[boot log]"
sudo tail -n 80 /tmp/psdk_boot.log 2>/dev/null || true
echo "[attempt log]"
sudo tail -n 80 /tmp/psdk_attempt.log 2>/dev/null || true
echo "[udp 5555]"
ss -lunp | grep 5555 || true
echo "[usb0]"
ip addr show usb0 || true
echo "[carrier]"
cat /sys/class/net/usb0/carrier 2>/dev/null || true
EOF
}

fetch_once() {
    ssh -o StrictHostKeyChecking=no -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOST}" 'python3 - <<'"'"'PY'"'"'
import json
import socket
import time

addr = ("127.0.0.1", 5555)
reqs = [
    {"id": 1, "method": "system.ping", "params": {}},
    {"id": 2, "method": "drone.get_telemetry", "params": {}},
    {"id": 3, "method": "drone.get_battery_info", "params": {}},
    {"id": 4, "method": "drone.get_gimbal_angle", "params": {}},
    {"id": 5, "method": "drone.get_camera_state", "params": {}},
    {"id": 6, "method": "drone.get_rtk_status", "params": {}},
]

def call(sock, req):
    sock.sendto((json.dumps(req) + "\n").encode(), addr)
    data, _ = sock.recvfrom(8192)
    return json.loads(data.decode())

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(4.0)

out = {
    "timestamp": time.strftime("%F %T"),
    "ok": True,
    "results": {},
}

try:
    for req in reqs:
        out["results"][req["method"]] = call(sock, req)
except Exception as exc:
    out["ok"] = False
    out["error"] = str(exc)

print(json.dumps(out, ensure_ascii=False))
PY'
}

render_once() {
    local payload="$1"

    python3 - "$payload" <<'PY'
import json
import sys

payload = json.loads(sys.argv[1])
print("=" * 72)
print(payload["timestamp"])

if not payload.get("ok"):
    print(f"status: ERROR  {payload.get('error', 'unknown error')}")
    sys.exit(0)

results = payload["results"]

def pick(method):
    msg = results.get(method, {})
    if "result" in msg:
        return msg["result"]
    if "error" in msg:
        return {"error": msg["error"]}
    return {}

ping = pick("system.ping")
tele = pick("drone.get_telemetry")
batt = pick("drone.get_battery_info")
gimbal = pick("drone.get_gimbal_angle")
camera = pick("drone.get_camera_state")
rtk = pick("drone.get_rtk_status")

print(f"ping:      {ping}")
print(f"telemetry: lat={tele.get('lat')} lon={tele.get('lon')} alt={tele.get('alt_rel_m')} "
      f"vx={tele.get('vx_ms')} vy={tele.get('vy_ms')} vz={tele.get('vz_ms')} "
      f"heading={tele.get('heading_deg')} gps={tele.get('gps_sats')}/{tele.get('gps_fix')} "
      f"flight={tele.get('flight_status')} motors={tele.get('motors_on')}")
print(f"battery:   pct={batt.get('remaining_pct')} voltage_mv={batt.get('voltage_mv')} "
      f"current_ma={batt.get('current_ma')} temp_dc={batt.get('temperature_dc')}")
print(f"gimbal:    pitch={gimbal.get('pitch')} roll={gimbal.get('roll')} yaw={gimbal.get('yaw')}")
print(f"camera:    mode={camera.get('mode')} recording={camera.get('is_recording')} "
      f"zoom={camera.get('zoom_factor')}")
print(f"rtk:       enabled={rtk.get('enabled')} fix_type={rtk.get('fix_type')} "
      f"sats={rtk.get('satellites')} lat={rtk.get('lat')} lon={rtk.get('lon')}")
PY
}

main() {
    print_header

    while true; do
        if payload="$(fetch_once 2>/dev/null)"; then
            render_once "$payload"
        else
            echo "========================================================================"
            echo "$(date '+%F %T')"
            echo "status: ERROR  ssh failed or OrangePi did not return status"
            fetch_diag 2>/dev/null || true
        fi
        sleep "$INTERVAL"
    done
}

main "$@"

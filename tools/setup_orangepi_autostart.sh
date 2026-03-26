#!/bin/bash
# setup_orangepi_autostart.sh -- one-shot installer for OrangePi PSDK autostart
set -euo pipefail

REPO_DIR="${REPO_DIR:-/home/orangepi/PSDK}"
SERVICE_NAME="psdkd.service"
SERVICE_SRC="$REPO_DIR/tools/$SERVICE_NAME"
SERVICE_DST="/etc/systemd/system/$SERVICE_NAME"
START_SCRIPT="$REPO_DIR/tools/start_dual.sh"
WATCH_SCRIPT="$REPO_DIR/tools/watch_drone_status_local.sh"
OPEN_TERM_SCRIPT="$REPO_DIR/tools/open_drone_status_terminal.sh"
DO_START=1

usage() {
    cat <<EOF
Usage: sudo bash tools/setup_orangepi_autostart.sh [--no-start] [--repo-dir PATH]

Options:
  --no-start       Install and enable only. Do not restart the service now.
  --repo-dir PATH  Override repository path. Default: $REPO_DIR
  --help           Show this help.
EOF
}

log() {
    echo "[setup] $*"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --no-start)
            DO_START=0
            shift
            ;;
        --repo-dir)
            if [ $# -lt 2 ]; then
                echo "ERROR: --repo-dir requires a path" >&2
                exit 1
            fi
            REPO_DIR="$2"
            SERVICE_SRC="$REPO_DIR/tools/$SERVICE_NAME"
            START_SCRIPT="$REPO_DIR/tools/start_dual.sh"
            WATCH_SCRIPT="$REPO_DIR/tools/watch_drone_status_local.sh"
            OPEN_TERM_SCRIPT="$REPO_DIR/tools/open_drone_status_terminal.sh"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: please run as root (sudo)." >&2
    exit 1
fi

if [ ! -d "$REPO_DIR" ]; then
    echo "ERROR: repo dir not found: $REPO_DIR" >&2
    exit 1
fi

if [ ! -f "$SERVICE_SRC" ]; then
    echo "ERROR: missing service file: $SERVICE_SRC" >&2
    exit 1
fi

if [ ! -f "$START_SCRIPT" ]; then
    echo "ERROR: missing startup script: $START_SCRIPT" >&2
    exit 1
fi

log "installing service file to $SERVICE_DST"
install -m 644 "$SERVICE_SRC" "$SERVICE_DST"

log "ensuring helper scripts are executable"
chmod +x "$START_SCRIPT"
[ -f "$WATCH_SCRIPT" ] && chmod +x "$WATCH_SCRIPT"
[ -f "$OPEN_TERM_SCRIPT" ] && chmod +x "$OPEN_TERM_SCRIPT"

log "reloading systemd"
systemctl daemon-reload

log "enabling $SERVICE_NAME"
systemctl enable "$SERVICE_NAME"

if [ "$DO_START" -eq 1 ]; then
    log "restarting $SERVICE_NAME"
    systemctl restart "$SERVICE_NAME"
else
    log "skip restarting service (--no-start)"
fi

log "current enable state:"
systemctl is-enabled "$SERVICE_NAME" || true

log "current service status:"
systemctl status "$SERVICE_NAME" --no-pager -l || true

cat <<EOF

Done.

Useful commands:
  sudo systemctl status $SERVICE_NAME --no-pager -l
  sudo journalctl -u $SERVICE_NAME -f
  sudo tail -n 200 /tmp/psdk_boot.log
  sudo tail -n 200 /tmp/psdk_attempt.log
EOF

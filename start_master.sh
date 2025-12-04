#!/bin/bash

# Start MASTER without tee (master writes port.txt itself)
# Usage:
#   TARGET_WORKERS=3 ./start_master.sh
#   IFACE=lo TARGET_WORKERS=3 ./start_master.sh
#   ./start_master.sh --mode TLS
#   ./start_master.sh --mode noTLS
#   ./start_master.sh --mode auth

set -euo pipefail

############################################
# Argument parsing: optional --mode <val>
############################################
MODE="noTLS"   # default

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      if [[ $# -lt 2 ]]; then
        echo "ERROR: --mode requires a value (auth, noTLS, TLS)"
        exit 1
      fi
      MODE="$2"
      shift 2
      ;;
    --mode=*)
      MODE="${1#*=}"
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [--mode {auth|noTLS|TLS}]"
      exit 0
      ;;
    *)
      # stop option parsing at first non-option
      break
      ;;
  esac
done

# Validate mode value
if [[ "$MODE" != "auth" && "$MODE" != "noTLS" && "$MODE" != "TLS" ]]; then
  echo "ERROR: invalid mode '$MODE' (must be: auth, noTLS, TLS)"
  exit 1
fi

echo "[master] MODE set to: $MODE"

############################################
# Environment and paths
############################################
APP_DIR="${APP_DIR:-$(pwd)}"
URI_FILE="${URI_FILE:-$APP_DIR/ompi-uri.txt}"
MASTER_BIN="${MASTER_BIN:-$APP_DIR/build/master}"
TARGET_WORKERS="${TARGET_WORKERS:-3}"
IFACE="${IFACE:-}"   # e.g. lo, eth0, wg0

echo "[master] app dir: $APP_DIR"
echo "[master] target workers: $TARGET_WORKERS"

command -v mpirun >/dev/null || { echo "mpirun not found"; exit 1; }
command -v ompi-server >/dev/null || { echo "ompi-server not found"; exit 1; }

############################################
# Start ompi-server if not running
############################################
if ! pgrep -f "ompi-server" >/dev/null 2>&1; then
  echo "[master] starting ompi-server…"
  ompi-server --no-daemonize --report-uri "$URI_FILE" >/dev/null 2>&1 &
  sleep 1
fi

[[ -s "$URI_FILE" ]] || { echo "ERROR: $URI_FILE missing/empty"; exit 1; }
URI="$(cat "$URI_FILE")"
echo "[master] pmix URI: $URI"

############################################
# Build master if needed
############################################
if [[ ! -x "$MASTER_BIN" ]]; then
  echo "[master] building: $MASTER_BIN"
  make build/master
fi

############################################
# MCA flags
############################################
MCA=( --mca pmix_server_uri "$URI" )

if [[ -n "$IFACE" ]]; then
  MCA+=( --mca pmix_tcp_if_include "$IFACE"
        --mca pml ob1 --mca btl tcp,self --mca oob tcp
        --mca btl_tcp_if_include "$IFACE" --mca oob_tcp_if_include "$IFACE" )
fi

############################################
# Mode-specific behavior hooks
############################################
case "$MODE" in
  TLS)
    echo "[master] TLS mode enabled"
    ;;
  noTLS)
    echo "[master] noTLS mode enabled"
    ;;
  auth)
    echo "[master] auth mode enabled"
    ;;
esac

############################################
# Start the master via mpirun
############################################
echo "[master] starting…"
export TARGET_WORKERS
set -x
mpirun "${MCA[@]}" -np 1 "$MASTER_BIN" "$MODE"

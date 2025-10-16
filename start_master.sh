#!/usr/bin/env bash
# Start MASTER bez tee (master sam piše port.txt)
# Usage:
#   TARGET_WORKERS=3 ./start_master.sh
#   IFACE=lo TARGET_WORKERS=3 ./start_master.sh   # lokalno na loopback

set -euo pipefail

APP_DIR="${APP_DIR:-$(pwd)}"
URI_FILE="${URI_FILE:-$APP_DIR/ompi-uri.txt}"
MASTER_BIN="${MASTER_BIN:-$APP_DIR/master}"
MASTER_SRC="${MASTER_SRC:-$APP_DIR/master.c}"
TARGET_WORKERS="${TARGET_WORKERS:-3}"
IFACE="${IFACE:-}"   # npr. lo, eth0, wg0

echo "[master] app dir: $APP_DIR"
echo "[master] target workers: $TARGET_WORKERS"

command -v mpirun >/dev/null || { echo "mpirun not found"; exit 1; }
command -v ompi-server >/dev/null || { echo "ompi-server not found"; exit 1; }

# pokreni ompi-server ako već ne radi
if ! pgrep -f "ompi-server" >/dev/null 2>&1; then
  echo "[master] starting ompi-server…"
  ompi-server --no-daemonize --report-uri "$URI_FILE" >/dev/null 2>&1 &
  sleep 1
fi

[[ -s "$URI_FILE" ]] || { echo "ERROR: $URI_FILE missing/empty"; exit 1; }
URI="$(cat "$URI_FILE")"
echo "[master] pmix URI: $URI"

# build ako treba
if [[ -f "$MASTER_SRC" ]] && { [[ ! -x "$MASTER_BIN" ]] || [[ "$MASTER_SRC" -nt "$MASTER_BIN" ]]; }; then
  echo "[master] building: $MASTER_SRC -> $MASTER_BIN"
  mpicc -O2 -std=gnu11 -o "$MASTER_BIN" "$MASTER_SRC"
fi

# MCA flagovi
MCA=( --mca pmix_server_uri "$URI" )
if [[ -n "$IFACE" ]]; then
  MCA+=( --mca pmix_tcp_if_include "$IFACE"
        --mca pml ob1 --mca btl tcp,self --mca oob tcp
        --mca btl_tcp_if_include "$IFACE" --mca oob_tcp_if_include "$IFACE" )
fi

echo "[master] starting…"
export TARGET_WORKERS
set -x
mpirun "${MCA[@]}" -np 1 "$MASTER_BIN"

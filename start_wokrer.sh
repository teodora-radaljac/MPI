#!/usr/bin/env bash
# Start WORKER koji koristi ompi-uri.txt i port.txt
# Usage:
#   ./start_worker.sh [ompi-uri.txt] [port.txt]
#   IFACE=lo ./start_worker.sh

set -euo pipefail

APP_DIR="${APP_DIR:-$(pwd)}"
URI_FILE="${1:-$APP_DIR/ompi-uri.txt}"
PORT_FILE="${2:-$APP_DIR/port.txt}"
WORKER_BIN="${WORKER_BIN:-$APP_DIR/worker}"
WORKER_SRC="${WORKER_SRC:-$APP_DIR/worker.c}"
IFACE="${IFACE:-}"   # npr. lo, eth0, wg0

echo "[worker] app dir: $APP_DIR"
[[ -s "$URI_FILE" ]]  || { echo "ERROR: URI file '$URI_FILE' missing/empty"; exit 1; }
[[ -s "$PORT_FILE" ]] || { echo "ERROR: PORT file '$PORT_FILE' missing/empty"; exit 1; }

URI="$(cat "$URI_FILE")"
PORT="$(head -n1 "$PORT_FILE" | tr -d $'\r')"

echo "[worker] pmix URI: $URI"
echo "[worker] master PORT: $PORT"

# build ako treba
if [[ -f "$WORKER_SRC" ]] && { [[ ! -x "$WORKER_BIN" ]] || [[ "$WORKER_SRC" -nt "$WORKER_BIN" ]]; }; then
  echo "[worker] building: $WORKER_SRC -> $WORKER_BIN"
  mpicc -O2 -std=gnu11 -o "$WORKER_BIN" "$WORKER_SRC"
fi

# MCA flagovi
MCA=( --mca pmix_server_uri "$URI" )
if [[ -n "$IFACE" ]]; then
  MCA+=( --mca pmix_tcp_if_include "$IFACE"
        --mca pml ob1 --mca btl tcp,self --mca oob tcp
        --mca btl_tcp_if_include "$IFACE" --mca oob_tcp_if_include "$IFACE" )
fi

echo "[worker] starting…"
set -x
mpirun "${MCA[@]}" -np 1 "$WORKER_BIN" "$PORT"

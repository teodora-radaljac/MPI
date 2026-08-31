#!/bin/bash
# Start WORKER koji koristi ompi-uri.txt i port.txt
# Usage:
#   ./start_worker.sh [--mode {auth|noTLS|TLS}] [ompi-uri.txt] [port.txt]
#   IFACE=lo ./start_worker.sh
#
# Default mode: auth

set -euo pipefail

############################################
# Argument parsing: optional --mode <val>
############################################
MODE="noTLS"   # default

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      if [[ $# -lt 2 ]]; then
        echo "ERROR: --mode requires a value (auth, noTLS, or TLS)"
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
      echo "Usage: $0 [--mode {auth|noTLS|TLS}] [ompi-uri.txt] [port.txt]"
      exit 0
      ;;
    *)
      # positional args are URI_FILE and PORT_FILE
      break
      ;;
  esac
done

# Validate mode
if [[ "$MODE" != "auth" && "$MODE" != "noTLS" && "$MODE" != "TLS" ]]; then
  echo "ERROR: invalid mode '$MODE' (must be 'auth', 'noTLS', or 'TLS')"
  exit 1
fi

echo "[worker] MODE set to: $MODE"

############################################
# Remaining positional arguments
############################################
APP_DIR="${APP_DIR:-$(pwd)}"
URI_FILE="${1:-$APP_DIR/ompi-uri.txt}"
PORT_FILE="${2:-$APP_DIR/port.txt}"
WORKER_BIN="${WORKER_BIN:-$APP_DIR/build/worker}"
IFACE="${IFACE:-}"   # npr. lo, eth0, wg0

echo "[worker] app dir: $APP_DIR"
[[ -s "$URI_FILE" ]]  || { echo "ERROR: URI file '$URI_FILE' missing/empty"; exit 1; }
[[ -s "$PORT_FILE" ]] || { echo "ERROR: PORT file '$PORT_FILE' missing/empty"; exit 1; }

URI="$(cat "$URI_FILE")"
PORT="$(head -n1 "$PORT_FILE" | tr -d $'\r')"

echo "[worker] pmix URI: $URI"
echo "[worker] master PORT: $PORT"

############################################
# Build worker if needed
############################################
if [[ ! -x "$WORKER_BIN" ]]; then
  echo "[worker] building: $WORKER_BIN"
  make build/worker
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
# Mode-specific behavior
############################################
if [[ "$MODE" == "TLS" ]]; then
  echo "[worker] TLS mode enabled"
elif [[ "$MODE" == "auth" ]]; then
  echo "[worker] AUTH mode enabled"
else
  echo "[worker] noTLS mode enabled"
fi 

############################################
# Start worker
############################################
echo "[worker] starting…"
set -x
mpirun "${MCA[@]}" -np 1 "$WORKER_BIN" "$PORT" "$MODE"

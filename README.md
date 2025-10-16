pkill -f 'mpirun|ompi-server|master|worker' || true
rm -f ompi-uri.txt port.txt
ompi-server --no-daemonize --report-uri "$(pwd)/ompi-uri.txt" &
sleep 1
IFACE=lo TARGET_WORKERS=3 ./start_master.sh
IFACE=lo ./start_worker.sh ompi-uri.txt port.txt

#!/usr/bin/env bash
set -euo pipefail

root=/home/teach/wt/riderguard
cd "$root"

listening() {
    ss -ltnH "( sport = :$1 )" | grep -q .
}

wait_for_port() {
    local port="$1" attempts="${2:-45}"
    for ((i = 0; i < attempts; i++)); do
        if listening "$port"; then return 0; fi
        sleep 1
    done
    echo "Port $port did not open; inspect logs under $root" >&2
    return 1
}

# The YOLO process can allocate shared GPU memory. Start it separately only
# after checking the host's running GPU workloads and available capacity.
if ! listening 18765; then
    echo 'YOLO is not listening on 127.0.0.1:18765; start it explicitly after checking GPU capacity.' >&2
    exit 1
fi

if ! listening 33306; then
    nohup runtime/mysql/mysqld --no-defaults --user=teach \
        --datadir="$root/mysql/datadir" --socket="$root/mysql/mysql.sock" \
        --port=33306 --bind-address=127.0.0.1 \
        --pid-file="$root/mysql/mysql.pid" --log-error="$root/logs/mysql.log" \
        --tmpdir="$root/mysql/tmp" --secure-file-priv="$root/mysql/tmp" \
        --skip-log-bin --innodb-buffer-pool-size=128M \
        > /dev/null 2>&1 < /dev/null &
fi
wait_for_port 33306

if ! listening 16379; then
    LD_LIBRARY_PATH="$root/runtime/redis/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        runtime/redis/usr/bin/redis-server config/redis.conf
fi
wait_for_port 16379

if ! listening 8091; then
    nohup python3 ai_adapter.py > logs/ai-adapter.log 2>&1 < /dev/null &
fi
wait_for_port 8091

if ! listening 18080; then
    nohup ./start-backend.sh > logs/backend.log 2>&1 < /dev/null &
fi
wait_for_port 18080 60

if ! listening 18766; then
    /usr/sbin/nginx -p "$root/" -c config/nginx.conf
fi
wait_for_port 18766

echo 'RiderGuard is ready at http://127.0.0.1:18766/'

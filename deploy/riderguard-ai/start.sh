#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
umask 077
set -a
source .env
set +a

if [[ -f service.pid ]] && kill -0 "$(cat service.pid)" 2>/dev/null; then
  echo "RiderGuard AI is already running (PID $(cat service.pid))."
  exit 0
fi
if ss -ltnH '( sport = :8091 )' | grep -q .; then
  echo "Port 8091 is occupied."
  exit 1
fi

mkdir -p logs
nohup .venv/bin/python -m uvicorn ai.server:app --host 127.0.0.1 --port 8091 \
  >> logs/service.log 2>&1 < /dev/null &
echo "$!" > service.pid
sleep 3
if ! kill -0 "$(cat service.pid)" 2>/dev/null; then
  tail -n 20 logs/service.log
  rm -f service.pid
  exit 1
fi
echo "RiderGuard AI started on 127.0.0.1:8091 (PID $(cat service.pid))."

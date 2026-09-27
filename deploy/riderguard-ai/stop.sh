#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [[ ! -f service.pid ]]; then
  echo "RiderGuard AI is not running."
  exit 0
fi
pid="$(cat service.pid)"
if ! kill -0 "$pid" 2>/dev/null; then
  rm -f service.pid
  echo "RiderGuard AI is not running."
  exit 0
fi
cmd="$(tr '\0' ' ' < "/proc/$pid/cmdline")"
if [[ "$cmd" != *"uvicorn ai.server:app"* ]]; then
  echo "PID $pid is not the RiderGuard AI service; refusing to stop it."
  exit 1
fi
kill "$pid"
rm -f service.pid
echo "RiderGuard AI stopped."

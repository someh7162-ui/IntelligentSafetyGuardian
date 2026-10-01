"""Local RiderGuard device API smoke test. Uses one disposable device."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import pymysql

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from device.simulator import image_item, send, telemetry_item  # noqa: E402


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def fault_ai_server(port: int) -> tuple[ThreadingHTTPServer, dict[str, str]]:
    state = {"mode": "ok"}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args: object) -> None:
            pass

        def respond(self, payload: dict) -> None:
            body = json.dumps(payload).encode()
            try:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_GET(self) -> None:
            self.respond({"status": "ok", "mode": "smoke"})

        def do_POST(self) -> None:
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            if state["mode"] == "slow":
                time.sleep(10)
            hint = int(parse_qs(urlparse(self.path).query).get("hint", ["0"])[0])
            self.respond({"personCount": hint, "mode": "mock"})

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, state


def admin_session(base: str, password: str) -> tuple[str, str]:
    import redis
    from Crypto.Cipher import AES, PKCS1_v1_5
    from Crypto.PublicKey import RSA
    from Crypto.Util.Padding import pad

    settings = {}
    for line in (ROOT / "admin" / "ruoyi" / "frontend" / ".env.development").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            settings[key.strip()] = value.strip().strip("'\"")
    client_id = settings["VITE_APP_CLIENT_ID"]
    captcha = json.load(urllib.request.urlopen(base + "/auth/code", timeout=5))["data"]
    code = redis.Redis(host="127.0.0.1", password="ruoyi123").get("global:captcha_codes:" + captcha["uuid"])
    if code is None:
        raise AssertionError("captcha was not saved to Redis")
    aes_key = uuid.uuid4().hex.encode()
    public_key = RSA.import_key(base64.b64decode(settings["VITE_APP_RSA_PUBLIC_KEY"]))
    key_header = base64.b64encode(PKCS1_v1_5.new(public_key).encrypt(base64.b64encode(aes_key))).decode()
    payload = {"username": "admin", "password": password, "code": json.loads(code),
               "uuid": captcha["uuid"], "clientId": client_id, "grantType": "password"}
    encrypted = base64.b64encode(AES.new(aes_key, AES.MODE_ECB).encrypt(pad(json.dumps(payload).encode(), 16)))
    request = urllib.request.Request(base + "/auth/login", data=encrypted, method="POST",
                                     headers={"Content-Type": "application/json", "clientid": client_id,
                                              "encrypt-key": key_header})
    result = json.load(urllib.request.urlopen(request, timeout=10))
    token = result.get("data", {}).get("access_token")
    check(bool(token), "captcha and admin login work")
    return token, client_id


def admin_request(base: str, token: str, client_id: str, path: str,
                  method: str = "GET", body: dict | None = None) -> dict | bytes:
    headers = {"Authorization": "Bearer " + token, "clientid": client_id}
    data = None if body is None else json.dumps(body).encode()
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()
        return raw if response.headers.get_content_type() == "image/jpeg" else json.loads(raw)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8080")
    parser.add_argument("--db-password", default=os.getenv("RIDERGUARD_TEST_DB_PASSWORD", "root"))
    parser.add_argument("--fault-ai-port", type=int, help="Serve controllable AI on this port; backend AI URL must match")
    parser.add_argument("--traffic-demo", action="store_true", help="Check DEMO-001 mock red signal warning (backend demo mode required)")
    parser.add_argument("--admin-password", default=os.getenv("RIDERGUARD_TEST_ADMIN_PASSWORD"),
                        help="Enable admin API checks using the local admin account")
    args = parser.parse_args()
    device_id = "IT-" + uuid.uuid4().hex[:12]
    token = uuid.uuid4().hex + uuid.uuid4().hex
    db = pymysql.connect(host="127.0.0.1", port=3306, user="root", password=args.db_password,
                         database="ry-vue", autocommit=True)
    fault_server, ai_state = fault_ai_server(args.fault_ai_port) if args.fault_ai_port else (None, None)
    queue_file = ROOT / "device" / ".queue" / f"{device_id}.jsonl"
    upload_root = (ROOT / "deploy" / "local-runtime" / "uploads").resolve()
    try:
        with db.cursor() as cursor:
            cursor.execute("INSERT INTO rg_device (device_id,token_hash) VALUES (%s,%s)",
                           (device_id, hashlib.sha256(token.encode()).hexdigest()))
        try:
            send(args.base_url, "invalid", telemetry_item(device_id, 0, 18, 34.2304, 108.9342))
            raise AssertionError("invalid device token was accepted")
        except urllib.error.HTTPError as error:
            check(error.code in (401, 403), "invalid token is rejected")

        normal = telemetry_item(device_id, 1, 18, 34.2304, 108.9342)
        response = send(args.base_url, token, normal)
        check(response["mode"] == "NORMAL" and not response["overspeed"], "normal speed uses normal limit")
        send(args.base_url, token, image_item(device_id, 4))
        send(args.base_url, token, image_item(device_id, 4))
        crowded = telemetry_item(device_id, 2, 14, 34.2305, 108.9343)
        response = send(args.base_url, token, crowded)
        check(response["mode"] == "CROWD" and response["overspeed"] and response["alert"],
              "two photos lower the limit and overspeed alerts")
        send(args.base_url, token, crowded)
        invalid = telemetry_item(device_id, 3, 0, 34.2306, 108.9344)
        payload = json.loads(base64.b64decode(invalid["body"]))
        payload.update(latitude=None, longitude=None, gpsValid=False)
        invalid["body"] = base64.b64encode(json.dumps(payload).encode()).decode()
        send(args.base_url, token, invalid)

        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM rg_track WHERE device_id=%s", (device_id,))
            check(cursor.fetchone()[0] == 3, "duplicate telemetry is idempotent")
            cursor.execute("SELECT COUNT(*),MIN(image_id) FROM rg_event WHERE device_id=%s", (device_id,))
            count, image_id = cursor.fetchone()
            check(count == 1 and image_id is not None, "one event contains photo evidence")
            cursor.execute("SELECT gps_valid,lat,lng FROM rg_track WHERE device_id=%s AND sample_id=%s",
                           (device_id, payload["sampleId"]))
            gps_valid, lat, lng = cursor.fetchone()
            check(gps_valid == 0 and lat is None and lng is None, "invalid GPS is stored without coordinates")

        if args.traffic_demo:
            # The mock signal is red for the first half of each minute. Use a fresh sample inside that window.
            deadline = time.monotonic() + 35
            while int(time.time() * 1000) % 60_000 >= 29_000:
                if time.monotonic() >= deadline:
                    raise AssertionError("DEMO-001 did not enter its red phase")
                time.sleep(0.5)
            approach = telemetry_item(device_id, 30, 18, 34.2309, 108.93495)
            signal_response = send(args.base_url, token, approach)
            signal = signal_response["trafficSignal"]
            check(signal["available"] and signal["state"] == "RED" and
                  signal["intersectionId"] == "DEMO-001" and signal["mock"] and
                  signal_response["alerts"]["trafficSignal"], "DEMO-001 red approach alerts device")
            send(args.base_url, token, approach)
            with db.cursor() as cursor:
                cursor.execute("SELECT id,track_id,is_mock,signal_state FROM rg_event "
                               "WHERE device_id=%s AND event_type='RED_SIGNAL_WARNING'", (device_id,))
                signal_events = cursor.fetchall()
                check(len(signal_events) == 1 and signal_events[0][2] == 1 and signal_events[0][3] == "RED",
                      "mock red warning is stored once in risk events")
                cursor.execute("SELECT COUNT(*) FROM rg_traffic_signal_event WHERE risk_event_id=%s",
                               (signal_events[0][0],))
                check(cursor.fetchone()[0] == 1, "mock signal snapshot is linked to risk event")
            if args.admin_password:
                admin_token, client_id = admin_session(args.base_url, args.admin_password)
                listed = admin_request(args.base_url, admin_token, client_id, "/riderguard/events")["data"]
                check(any(event["id"] == signal_events[0][0] and
                          event["event_type"] == "RED_SIGNAL_WARNING" and event["is_mock"] for event in listed),
                      "admin event list includes marked mock red warning")
                detail = admin_request(args.base_url, admin_token, client_id,
                                       f"/riderguard/events/{signal_events[0][0]}")["data"]
                check(detail["event_type"] == "RED_SIGNAL_WARNING" and detail["is_mock"] and
                      detail["signal_group"] == "DEMO-NE-STRAIGHT", "admin sees marked mock signal detail")

        if args.admin_password:
            admin_token, client_id = admin_session(args.base_url, args.admin_password)
            devices = admin_request(args.base_url, admin_token, client_id, "/riderguard/devices")["data"]
            check(any(device["device_id"] == device_id for device in devices), "admin sees simulated device")
            tracks = admin_request(args.base_url, admin_token, client_id,
                                   f"/riderguard/devices/{device_id}/tracks?fromMs=0&toMs={int(time.time()*1000)}")["data"]
            expected_tracks = 4 if args.traffic_demo else 3
            expected_fixes = 3 if args.traffic_demo else 2
            check(len(tracks) == expected_tracks and sum(bool(track["gps_valid"]) for track in tracks) == expected_fixes,
                  "admin receives valid and invalid trajectory points")
            events = admin_request(args.base_url, admin_token, client_id, "/riderguard/events")["data"]
            target_event = next(event for event in events if event["device_id"] == device_id and event["event_type"] == "OVERSPEED")
            detail = admin_request(args.base_url, admin_token, client_id,
                                   f"/riderguard/events/{target_event['id']}")["data"]
            check(detail["image_id"] is not None and detail["status"] == "OPEN", "admin sees event detail")
            photo = admin_request(args.base_url, admin_token, client_id,
                                  f"/riderguard/images/{detail['image_id']}")
            check(isinstance(photo, bytes) and photo.startswith(b"\xff\xd8"), "admin can read protected evidence")
            for status in ("REVIEWING", "RESOLVED"):
                admin_request(args.base_url, admin_token, client_id,
                              f"/riderguard/events/{target_event['id']}/process", "PUT",
                              {"status": status, "note": "本地联调验证"})
            detail = admin_request(args.base_url, admin_token, client_id,
                                   f"/riderguard/events/{target_event['id']}")["data"]
            check(detail["status"] == "RESOLVED" and len(detail["actions"]) == 2,
                  "admin processing writes two audit actions")
            health = admin_request(args.base_url, admin_token, client_id, "/riderguard/ai/health")["data"]
            check(health["status"] == "UP", "admin sees AI service health")

        offline = subprocess.run([sys.executable, str(ROOT / "device" / "simulator.py"),
                                  "--device-id", device_id, "--token", token,
                                  "--base-url", "http://127.0.0.1:59999", "--seconds", "2"],
                                 cwd=ROOT, capture_output=True, text=True, timeout=15)
        check(offline.returncode == 0 and queue_file.exists() and queue_file.stat().st_size > 0,
              "outage queues device packets")
        replay = subprocess.run([sys.executable, str(ROOT / "device" / "simulator.py"),
                                 "--device-id", device_id, "--token", token,
                                 "--base-url", args.base_url, "--seconds", "1"],
                                cwd=ROOT, capture_output=True, text=True, timeout=20)
        check(replay.returncode == 0 and queue_file.stat().st_size == 0,
              "reconnect replays and clears queued packets")
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM rg_track WHERE device_id=%s", (device_id,))
            check(cursor.fetchone()[0] >= 6, "replayed GPS points reach the backend")

        if ai_state is not None:
            for _ in range(3):
                send(args.base_url, token, image_item(device_id, 0))
            check(send(args.base_url, token, telemetry_item(device_id, 4, 0, 34.2307, 108.9345))["mode"] == "NORMAL",
                  "clear photos restore normal mode")
            failing_photo = image_item(device_id, 4)
            ai_state["mode"] = "slow"
            started = time.monotonic()
            failed = send(args.base_url, token, failing_photo)
            check(failed["status"] == "ERROR" and 7 <= time.monotonic() - started < 12,
                  "AI timeout marks photo failed within request timeout")
            check(send(args.base_url, token, telemetry_item(device_id, 5, 0, 34.2308, 108.9346))["mode"] == "NORMAL",
                  "failed recognition does not switch crowd mode")
            ai_state["mode"] = "ok"
            recovered = send(args.base_url, token, failing_photo)
            check(recovered["status"] == "READY" and recovered["personCount"] == 4 and
                  recovered["imageId"] == failed["imageId"],
                  "same photo ID retries after AI recovery")
            send(args.base_url, token, image_item(device_id, 4))
            check(send(args.base_url, token, telemetry_item(device_id, 6, 0, 34.2309, 108.9347))["mode"] == "CROWD",
                  "recovered recognitions restore crowd detection")
            with db.cursor() as cursor:
                cursor.execute("SELECT COUNT(*) FROM rg_inference_attempt WHERE image_id=%s", (failed["imageId"],))
                check(cursor.fetchone()[0] == 2, "AI failure and recovery each produce one attempt record")
        print("RiderGuard local device integration passed")
    finally:
        if fault_server is not None:
            fault_server.shutdown()
            fault_server.server_close()
        with db.cursor() as cursor:
            cursor.execute("SELECT file_name FROM rg_image WHERE device_id=%s", (device_id,))
            names = [row[0] for row in cursor.fetchall()]
            cursor.execute("DELETE FROM rg_inference_attempt WHERE image_id IN "
                           "(SELECT id FROM rg_image WHERE device_id=%s)", (device_id,))
            cursor.execute("DELETE FROM rg_event_action WHERE event_id IN "
                           "(SELECT id FROM rg_event WHERE device_id=%s)", (device_id,))
            cursor.execute("DELETE FROM rg_traffic_signal_event WHERE device_id=%s", (device_id,))
            for table in ("rg_event", "rg_track", "rg_image", "rg_device"):
                cursor.execute(f"DELETE FROM {table} WHERE device_id=%s", (device_id,))
        for name in names:
            target = (upload_root / name).resolve()
            if target.parent == upload_root and target.suffix == ".jpg":
                target.unlink(missing_ok=True)
        queue_file.unlink(missing_ok=True)
        db.close()


if __name__ == "__main__":
    main()

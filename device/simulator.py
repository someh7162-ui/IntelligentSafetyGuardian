"""Simulate one RiderGuard device, including retryable telemetry and JPEG uploads.

python device/simulator.py --device-id SG-001 --token <token from admin page> --seconds 90
"""

from __future__ import annotations

import argparse
import base64
import json
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


def send(base: str, token: str, item: dict) -> dict:
    url = base.rstrip("/") + item["path"]
    body = base64.b64decode(item["body"])
    headers = {"X-Device-Token": token, **item["headers"]}
    request = urllib.request.Request(url, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read())


def jpeg(people: int) -> bytes:
    import cv2
    import numpy as np

    frame = np.full((240, 320, 3), (222, 231, 236), dtype=np.uint8)
    cv2.rectangle(frame, (0, 140), (320, 240), (130, 136, 137), -1)
    for index in range(people):
        x = 35 + (index % 6) * 48
        y = 105 + (index // 6) * 35
        cv2.circle(frame, (x, y), 8, (45, 47, 50), -1)
        cv2.line(frame, (x, y + 8), (x, y + 35), (45, 47, 50), 5)
    ok, encoded = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 65])
    if not ok:
        raise RuntimeError("JPEG encoding failed")
    return encoded.tobytes()


def telemetry_item(device: str, tick: int, speed: float, lat: float, lng: float) -> dict:
    payload = {
        "deviceId": device, "sampleId": str(uuid.uuid4()), "capturedAtMs": int(time.time() * 1000),
        "latitude": lat, "longitude": lng, "speedKph": speed, "gpsValid": True,
    }
    return {"path": "/device/riderguard/telemetry", "body": base64.b64encode(json.dumps(payload).encode()).decode(),
            "headers": {"Content-Type": "application/json"}}


def image_item(device: str, people: int) -> dict:
    boundary = "riderguard" + uuid.uuid4().hex
    fields = {"deviceId": device, "sampleId": str(uuid.uuid4()), "capturedAtMs": str(int(time.time() * 1000))}
    parts = []
    for name, value in fields.items():
        parts.append(f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n{value}\r\n".encode())
    parts.append(f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"road.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n".encode())
    parts.append(jpeg(people))
    parts.append(f"\r\n--{boundary}--\r\n".encode())
    return {"path": "/device/riderguard/image", "body": base64.b64encode(b"".join(parts)).decode(),
            "headers": {"Content-Type": f"multipart/form-data; boundary={boundary}", "X-Demo-People": str(people)}}


def main() -> None:
    parser = argparse.ArgumentParser(description="RiderGuard demo rider")
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--token", required=True)
    parser.add_argument("--base-url", default="http://127.0.0.1:8080")
    parser.add_argument("--seconds", type=int, default=90)
    args = parser.parse_args()
    queue_file = Path(__file__).parent / ".queue" / f"{args.device_id}.jsonl"
    queue_file.parent.mkdir(exist_ok=True)
    queue = [json.loads(line) for line in queue_file.read_text().splitlines() if line] if queue_file.exists() else []
    policy = {"mode": "NORMAL", "limitKph": 25, "validUntilMs": 0}
    for tick in range(args.seconds):
        phase = tick % 60
        people = 4 if 12 <= phase < 38 else 0
        speed = 18 if phase < 20 else 14 if phase < 38 else 22
        # Replaying preserves sample IDs, so server-side unique keys make retries idempotent.
        queue.append(telemetry_item(args.device_id, tick, speed, 34.2304 + tick * 0.00002, 108.9342 + tick * 0.00003))
        if tick % 4 == 0:
            queue.append(image_item(args.device_id, people))
        pending = []
        for index, item in enumerate(queue):
            try:
                reply = send(args.base_url, args.token, item)
                if item["path"].endswith("/image") and reply.get("status") == "ERROR":
                    raise OSError("inference unavailable; retrying saved image")
                if item["path"].endswith("/telemetry"):
                    policy = reply
                else:
                    print(f"photo: {reply}")
            except urllib.error.HTTPError as error:
                if 400 <= error.code < 500:
                    raise RuntimeError(f"Device request rejected ({error.code}); check token and demo mode") from error
                pending = queue[index:]
                print(f"server error; queued {len(pending)} packets: {error}")
                break
            except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError) as error:
                pending = queue[index:]
                print(f"network unavailable; queued {len(pending)} packets: {error}")
                break
        queue = pending
        queue_file.write_text("".join(json.dumps(item) + "\n" for item in queue))
        valid = policy.get("validUntilMs", 0) >= int(time.time() * 1000)
        over = speed > policy.get("limitKph", 25)
        print(f"{tick:03d}s | GPS 34.2304,108.9342 | {speed:.1f} km/h | people={people} | "
              f"{policy.get('mode')} limit={policy.get('limitKph')} | "
              f"LED={'RED' if over else 'GREEN'} BUZZER={'ON' if over else 'OFF'}" +
              (" | POLICY STALE" if not valid else ""))
        time.sleep(1)


if __name__ == "__main__":
    main()

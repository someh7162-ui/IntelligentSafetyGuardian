"""STM32 USB -> official RiderGuard HTTP API. This program never controls a 4G UART.

The serial reader, telemetry uploader and JPEG uploader run independently so a slow
inference request cannot block GNSS reception. Controlled mode accepts JSON lines
on stdin and emits redacted JSON events on stdout for the management web page.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import json
import math
import os
from pathlib import Path
import queue
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


MAX_JPEG = 1_048_576
JPEG_MARKER = b'JPEG_TX_BEGIN LEN='
GPS_MARKER = b'GPS_NMEA '
LBS_MARKER = b'CELL_LOCATION '


def now_ms() -> int:
    return time.time_ns() // 1_000_000


def parse_rmc(sentence: str) -> dict | None:
    match = re.fullmatch(r'\$([A-Z]{2}RMC,[^*\r\n]+)\*([0-9a-fA-F]{2})', sentence.strip())
    if not match:
        return None
    checksum = 0
    for byte in match[1].encode('ascii'):
        checksum ^= byte
    if checksum != int(match[2], 16):
        return None
    fields = match[1].split(',')
    if len(fields) < 10:
        return None
    empty = dict(gpsValid=False, latitude=None, longitude=None, speedKph=0.0, heading=None, gpsAccuracy=None)
    if fields[2] == 'V':
        return empty
    if fields[2] != 'A' or (len(fields) > 12 and fields[12] == 'N'):
        return None

    def coordinate(value: str, hemisphere: str, latitude: bool) -> float:
        width = 4 if latitude else 5
        if not re.fullmatch(r'\d{' + str(width) + r'}(?:\.\d+)?', value):
            raise ValueError('Invalid coordinate')
        degree_digits = 2 if latitude else 3
        degrees, minutes = int(value[:degree_digits]), Decimal(value[degree_digits:])
        limit = 90 if latitude else 180
        if hemisphere not in ('NS' if latitude else 'EW') or len(hemisphere) != 1:
            raise ValueError('Invalid hemisphere')
        if minutes >= 60 or degrees > limit or (degrees == limit and minutes != 0):
            raise ValueError('Coordinate out of range')
        result = float(Decimal(degrees) + minutes / 60)
        return -result if hemisphere in 'SW' else result

    try:
        lat = coordinate(fields[3], fields[4], True)
        lng = coordinate(fields[5], fields[6], False)
        if not re.fullmatch(r'\d+(?:\.\d+)?', fields[7]):
            return None
        speed = float(Decimal(fields[7]) * Decimal('1.852'))
        if not math.isfinite(speed) or not 0 <= speed <= 150:
            return None
        heading = float(fields[8]) if fields[8] else None
        if heading is not None and (not math.isfinite(heading) or not 0 <= heading < 360):
            heading = None
        return dict(gpsValid=True, latitude=lat, longitude=lng, speedKph=speed, heading=heading, gpsAccuracy=None)
    except (ValueError, InvalidOperation):
        return None


class UsbDecoder:
    """Length-framed JPEG payloads are opaque, even when they contain NMEA markers."""
    def __init__(self):
        self.buffer = bytearray()
        self.frame_time: int | None = None

    def feed(self, chunk: bytes, received_ms: int | None = None) -> list[tuple[str, object, int]]:
        received_ms = now_ms() if received_ms is None else received_ms
        self.buffer.extend(chunk)
        events = []
        while self.buffer:
            image, gps, lbs = self.buffer.find(JPEG_MARKER), self.buffer.find(GPS_MARKER), self.buffer.find(LBS_MARKER)
            candidates = [position for position in (image, gps, lbs) if position >= 0]
            if not candidates:
                keep = max(len(JPEG_MARKER), len(GPS_MARKER), len(LBS_MARKER)) - 1
                if len(self.buffer) > keep:
                    del self.buffer[:-keep]
                self.frame_time = None
                break
            start = min(candidates)
            if start:
                del self.buffer[:start]
                self.frame_time = None
            newline = self.buffer.find(b'\n')
            if newline < 0:
                if len(self.buffer) > 200:
                    del self.buffer[:1]
                break
            header = bytes(self.buffer[:newline + 1]).strip()
            if self.buffer.startswith(LBS_MARKER):
                del self.buffer[:newline + 1]
                self.frame_time = None
                try:
                    location = json.loads(header[len(LBS_MARKER):])
                    lat, lng, age = location['latitude'], location['longitude'], location['ageMs']
                    if location.get('locationSource') != 'LBS' or location.get('gpsValid') is not False:
                        continue
                    if any(type(n) not in (float, int) or not math.isfinite(n) for n in (lat, lng)) or abs(lat) > 90 or abs(lng) > 180:
                        continue
                    if type(age) is not int or not 0 <= age <= 600000:
                        continue
                    events.append(('lbs', dict(latitude=lat, longitude=lng, ageMs=age), received_ms))
                except (ValueError, KeyError, TypeError):
                    pass
                continue
            if self.buffer.startswith(GPS_MARKER):
                del self.buffer[:newline + 1]
                self.frame_time = None
                try:
                    fix = parse_rmc(header[len(GPS_MARKER):].decode('ascii'))
                    if fix is not None:
                        events.append(('telemetry', fix, received_ms))
                except UnicodeDecodeError:
                    pass
                continue
            match = re.fullmatch(rb'JPEG_TX_BEGIN LEN=(\d{1,8})', header)
            if not match or not 4 <= int(match[1]) <= MAX_JPEG:
                del self.buffer[:newline + 1]
                self.frame_time = None
                continue
            if self.frame_time is None:
                self.frame_time = received_ms
            length = int(match[1])
            end = newline + 1 + length
            if len(self.buffer) < end:
                break
            jpeg = bytes(self.buffer[newline + 1:end])
            del self.buffer[:end]
            captured_ms, self.frame_time = self.frame_time, None
            if jpeg.startswith(b'\xff\xd8') and jpeg.endswith(b'\xff\xd9'):
                events.append(('image', jpeg, captured_ms))
        return events


class DeviceClient:
    def __init__(self, base_url: str, token: str, image_url: str | None = None, telemetry_url: str | None = None):
        if not re.fullmatch(r'[0-9a-f]{64}', token):
            raise ValueError('设备凭证必须为 64 位小写十六进制字符')
        self.token = token
        base = base_url.rstrip('/')
        self.image_url = self.validate_url(image_url or base + '/device/riderguard/image')
        self.telemetry_url = self.validate_url(telemetry_url or base + '/device/riderguard/telemetry')
        # Ignore desktop proxy environment settings for local device uploads.
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, request, fp, code, message, headers, new_url):
                return None
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    @staticmethod
    def validate_url(url: str) -> str:
        parsed = urllib.parse.urlsplit(url)
        if parsed.username or parsed.password or parsed.fragment or not parsed.hostname:
            raise ValueError('无效的设备接口地址')
        if parsed.scheme != 'https' and not (parsed.scheme == 'http' and parsed.hostname in ('127.0.0.1', 'localhost', '::1')):
            raise ValueError('设备接口必须使用 HTTPS；本机回环测试可使用 HTTP')
        return url

    def _post(self, url: str, body: bytes, content_type: str) -> dict:
        request = urllib.request.Request(url, data=body, method='POST', headers={
            'X-Device-Token': self.token, 'Content-Type': content_type})
        with self.opener.open(request, timeout=12) as response:
            payload = response.read(65537)
            if len(payload) > 65536:
                raise ValueError('后台响应过大')
            value = json.loads(payload)
            if not isinstance(value, dict):
                raise ValueError('后台响应格式错误')
            return value

    def telemetry(self, sample: dict) -> dict:
        result = self._post(self.telemetry_url, json.dumps(sample, separators=(',', ':'), allow_nan=False).encode(), 'application/json')
        limit, expires = result.get('limitKph'), result.get('validUntilMs')
        if result.get('mode') not in ('NORMAL', 'CROWD') or type(limit) not in (int, float) or not 0 < limit <= 80 or type(expires) is not int or expires <= 0:
            raise ValueError('后台未返回有效的正式设备策略')
        return result

    def image(self, sample: dict, jpeg: bytes) -> dict:
        boundary = 'RiderGuard' + uuid.uuid4().hex
        pieces = []
        for name in ('deviceId', 'sampleId', 'capturedAtMs'):
            pieces.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{sample[name]}\r\n'.encode())
        pieces.append(f'--{boundary}\r\nContent-Disposition: form-data; name="image"; filename="camera.jpg"\r\nContent-Type: image/jpeg\r\n\r\n'.encode())
        pieces.extend([jpeg, f'\r\n--{boundary}--\r\n'.encode()])
        result = self._post(self.image_url, b''.join(pieces), 'multipart/form-data; boundary=' + boundary)
        if result.get('status') not in ('READY', 'STALE', 'SAVED'):
            raise ValueError('照片已接收，识别尚未成功；将按同一采样编号重试')
        return result


class Gateway:
    def __init__(self, args, client: DeviceClient, output=print):
        self.args, self.client, self.output = args, client, output
        self.stop_event = threading.Event()
        self.auto_images = threading.Event()
        self.capture_requested = threading.Event()
        if not args.controlled:
            self.auto_images.set()
        self.output_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.telemetry_queue: queue.Queue = queue.Queue(maxsize=64)
        self.image_queue: queue.Queue = queue.Queue(maxsize=1)
        self.boot = uuid.uuid4().hex[:12]
        self.number = 0
        self.telemetry_ms, self.image_ms = args.telemetry_ms, args.image_ms
        self.next_telemetry = self.next_image = 0.0
        self.latest_lbs = None
        self.reader = None

    def emit(self, kind: str, message: str, **data):
        event = dict(kind=kind, message=message, timeMs=now_ms(), **data)
        text = re.sub(r'[0-9a-fA-F]{64}', '[redacted]', json.dumps(event, ensure_ascii=False, allow_nan=False))
        with self.output_lock:
            self.output(text, flush=True)

    def sample(self, kind: str, captured_ms: int) -> dict:
        self.number += 1
        return dict(deviceId=self.args.device_id, sampleId=f'{kind}-{self.boot}-{self.number}', capturedAtMs=captured_ms)

    def accept(self, kind: str, value, captured_ms: int):
        if kind == 'lbs':
            self.latest_lbs = (value, captured_ms - value['ageMs'])
            return
        monotonic = time.monotonic()
        # The board already schedules its frames. USB/OS jitter may deliver the
        # next nominal interval a few milliseconds early; do not skip a whole
        # hardware period because of a second, exact host-side rate limit.
        telemetry_slack = min(1.0, self.telemetry_ms / 10000)
        image_slack = min(1.0, self.image_ms / 10000)
        if kind == 'telemetry' and monotonic + telemetry_slack >= self.next_telemetry:
            location = dict(value, locationSource='GPS' if value['gpsValid'] else 'NONE', locationAgeMs=None)
            if not value['gpsValid'] and self.latest_lbs:
                coarse, measured_ms = self.latest_lbs
                age = captured_ms - measured_ms
                if 0 <= age <= 600000:
                    location.update(latitude=coarse['latitude'], longitude=coarse['longitude'],
                                    locationSource='LBS', locationAgeMs=age, speedKph=0, heading=None, gpsAccuracy=None)
            sample = dict(self.sample('gps', captured_ms), **location)
            if self.telemetry_queue.full():
                try:
                    self.telemetry_queue.get_nowait()
                    self.emit('warning', '离线定位队列已满，丢弃最旧采样')
                except queue.Empty:
                    pass
            self.telemetry_queue.put_nowait(sample)
            self.next_telemetry = monotonic + self.telemetry_ms / 1000
        elif kind == 'image' and (self.capture_requested.is_set() or (self.auto_images.is_set() and monotonic + image_slack >= self.next_image)):
            if self.image_queue.full():
                return
            self.capture_requested.clear()
            self.image_queue.put_nowait((self.sample('cam', captured_ms), value))
            self.next_image = monotonic + self.image_ms / 1000

    def read_serial(self):
        decoder = UsbDecoder()
        try:
            while not self.stop_event.is_set():
                chunk = self.reader.read(8192)
                if chunk:
                    for event in decoder.feed(chunk):
                        self.accept(*event)
        except Exception:
            if not self.stop_event.is_set():
                self.emit('error', 'USB 串口已断开或读取失败，请检查连接后重新开始')
                self.stop_event.set()

    def upload(self, kind: str):
        pending, retry_seconds = None, 1
        work = self.telemetry_queue if kind == 'telemetry' else self.image_queue
        while not self.stop_event.is_set():
            if pending is None:
                try:
                    pending = work.get(timeout=.2)
                except queue.Empty:
                    continue
            sample = pending if kind == 'telemetry' else pending[0]
            maximum_age = 300_000 if kind == 'telemetry' else 15_000
            if now_ms() - sample['capturedAtMs'] > maximum_age:
                self.emit('warning', '丢弃过期的' + ('定位采样' if kind == 'telemetry' else '照片'))
                pending, retry_seconds = None, 1
                continue
            try:
                if kind == 'telemetry':
                    policy = self.client.telemetry(sample)
                    with self.state_lock:
                        for name, attribute, lower, upper in [('telemetryIntervalMs', 'telemetry_ms', 1000, 60000), ('imageIntervalMs', 'image_ms', 3000, 300000)]:
                            value = policy.get(name)
                            if type(value) is int and lower <= value <= upper:
                                setattr(self, attribute, value)
                    self.emit('telemetry', '定位与速度已进入正式后台', telemetry=sample, policy=policy)
                else:
                    result = self.client.image(sample, pending[1])
                    self.emit('image', '照片已进入正式后台', result=result, sampleId=sample['sampleId'])
                pending, retry_seconds = None, 1
            except urllib.error.HTTPError as error:
                if error.code in (401, 403):
                    self.emit('error', '设备凭证被拒绝，请核对所选设备及凭证')
                    self.stop_event.set()
                    return
                self.emit('warning', f'{kind} 上报返回 HTTP {error.code}，将重试原采样')
                self.stop_event.wait(retry_seconds)
                retry_seconds = min(retry_seconds * 2, 15)
            except Exception:
                self.emit('warning', f'{kind} 上传或识别暂未成功，将重试原采样')
                self.stop_event.wait(retry_seconds)
                retry_seconds = min(retry_seconds * 2, 15)

    def control(self):
        for line in sys.stdin:
            if len(line) > 1024:
                continue
            try:
                message = json.loads(line)
                action = message.get('action')
                if action == 'stop':
                    self.stop_event.set()
                    return
                if action == 'capture':
                    self.capture_requested.set()
                    self.emit('control', '等待下一张 USB 摄像头帧')
                elif action == 'images':
                    enabled = message.get('enabled') is True
                    self.auto_images.set() if enabled else self.auto_images.clear()
                    if not enabled:
                        self.capture_requested.clear()
                        try:
                            self.image_queue.get_nowait()
                        except queue.Empty:
                            pass
                    self.emit('control', '自动图片上传已' + ('开启' if enabled else '暂停'), autoImages=enabled)
            except (ValueError, AttributeError):
                continue
        # Backend exit closes stdin; do not leave a detached uploader behind.
        self.stop_event.set()

    def run(self) -> int:
        import serial
        self.reader = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1, rtscts=False, dsrdtr=False)
        self.reader.dtr = self.reader.rts = False
        self.reader.port = self.args.port
        try:
            self.reader.open()
            self.emit('started', 'USB 设备已连接；定位开始上报，' + ('自动传图已开启' if self.auto_images.is_set() else '图片由网页控制'), port=self.args.port, deviceId=self.args.device_id, autoImages=self.auto_images.is_set())
            targets = [self.read_serial, lambda: self.upload('telemetry'), lambda: self.upload('image')]
            if self.args.controlled:
                targets.append(self.control)
            for target in targets:
                threading.Thread(target=target, daemon=True).start()
            while not self.stop_event.wait(.2):
                pass
            return 0
        except KeyboardInterrupt:
            return 0
        except Exception:
            self.emit('error', '无法打开 USB 串口，请检查端口号和是否被其他程序占用')
            return 1
        finally:
            self.stop_event.set()
            if self.reader.is_open:
                self.reader.close()
            self.emit('stopped', 'USB 测试已停止，串口已释放')


def main() -> int:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list-ports', action='store_true')
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--device-id', default='STM32-CAM-001')
    parser.add_argument('--base-url', default='http://127.0.0.1:8080')
    parser.add_argument('--image-url')
    parser.add_argument('--telemetry-url')
    parser.add_argument('--token-file', type=Path)
    parser.add_argument('--telemetry-ms', type=int, default=3000)
    parser.add_argument('--image-ms', type=int, default=30000)
    parser.add_argument('--controlled', action='store_true')
    args = parser.parse_args()
    if args.list_ports:
        from serial.tools import list_ports
        print(json.dumps([dict(port=p.device, description=p.description) for p in list_ports.comports()], ensure_ascii=False))
        return 0
    if not re.fullmatch(r'[A-Za-z0-9_-]{3,64}', args.device_id) or not re.fullmatch(r'COM[1-9]\d{0,2}', args.port):
        parser.error('设备编号或 COM 端口无效')
    if not 1000 <= args.telemetry_ms <= 60000 or not 3000 <= args.image_ms <= 300000:
        parser.error('上报间隔超出允许范围')
    token = os.environ.pop('RIDERGUARD_DEVICE_TOKEN', '').strip()
    if not token and args.token_file:
        token = args.token_file.read_text(encoding='utf-8-sig').strip()
    try:
        client = DeviceClient(args.base_url, token, args.image_url, args.telemetry_url)
        return Gateway(args, client).run()
    except (ValueError, OSError):
        print(json.dumps(dict(kind='error', message='网关配置或设备凭证无效', timeMs=now_ms()), ensure_ascii=False), flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

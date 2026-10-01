"""RiderGuard crowd contract backed by the existing local YOLO service."""
import hmac
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.request import Request, urlopen

KEY = Path('/home/teach/wt/riderguard/config/ai-key').read_text().strip()
YOLO_URL = 'http://127.0.0.1:18765/infer'

class Handler(BaseHTTPRequestHandler):
    def reply(self, status, payload):
        data = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == '/health':
            self.reply(200, {'status': 'ok', 'mode': 'yolo'})
        else:
            self.reply(404, {'error': 'not found'})

    def do_POST(self):
        if self.path != '/infer/crowd':
            self.reply(404, {'error': 'not found'})
            return
        if not hmac.compare_digest(self.headers.get('X-Internal-Key', ''), KEY):
            self.reply(401, {'error': 'invalid key'})
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            length = 0
        if not 4 <= length <= 1_048_576:
            self.reply(400, {'error': 'expected JPEG <= 1 MB'})
            return
        image = self.rfile.read(length)
        if not (image.startswith(b'\xff\xd8') and image.endswith(b'\xff\xd9')):
            self.reply(400, {'error': 'invalid JPEG'})
            return
        try:
            request = Request(YOLO_URL, data=image, headers={'Content-Type': 'image/jpeg'})
            with urlopen(request, timeout=6) as response:
                result = json.load(response)
            detections = result.get('detections', [])
            count = sum(1 for detection in detections
                        if detection.get('label') == 'person'
                        and float(detection.get('confidence', 0)) >= 0.5)
            self.reply(200, {'personCount': count, 'mode': 'yolo'})
        except Exception as error:
            self.reply(502, {'error': type(error).__name__})

if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', 8091), Handler).serve_forever()

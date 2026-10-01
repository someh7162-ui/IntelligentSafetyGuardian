"""CPU-only concurrency contract for the wt YOLO adapter."""
import importlib.util
import json
import os
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class StubYolo(BaseHTTPRequestHandler):
    started = threading.Event()
    release = threading.Event()

    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        self.started.set()
        if not self.release.wait(5):
            self.send_error(504)
            return
        payload = json.dumps({'detections': [
            {'label': 'person', 'confidence': 0.8},
            {'label': 'person', 'confidence': 0.9},
            {'label': 'person', 'confidence': 0.7},
        ]}).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


class AdapterConcurrencyTest(unittest.TestCase):
    def test_only_one_inference_reaches_upstream(self):
        StubYolo.started.clear()
        StubYolo.release.clear()
        with tempfile.TemporaryDirectory() as temp:
            key_file = Path(temp, 'ai-key')
            key_file.write_text('test-key')
            upstream = ThreadingHTTPServer(('127.0.0.1', 0), StubYolo)
            upstream_thread = threading.Thread(target=upstream.serve_forever, daemon=True)
            upstream_thread.start()
            os.environ['RIDERGUARD_AI_KEY_FILE'] = str(key_file)
            os.environ['RIDERGUARD_YOLO_URL'] = f'http://127.0.0.1:{upstream.server_port}/infer'
            os.environ['RIDERGUARD_AI_MAX_INFLIGHT'] = '1'
            spec = importlib.util.spec_from_file_location('test_adapter', Path(__file__).with_name('ai_adapter.py'))
            adapter_module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(adapter_module)
            adapter = ThreadingHTTPServer(('127.0.0.1', 0), adapter_module.Handler)
            adapter_thread = threading.Thread(target=adapter.serve_forever, daemon=True)
            adapter_thread.start()
            url = f'http://127.0.0.1:{adapter.server_port}/infer/crowd'
            jpeg = b'\xff\xd8test\xff\xd9'
            results = []

            def first_request():
                with urlopen(Request(url, data=jpeg, headers={'X-Internal-Key': 'test-key'}), timeout=8) as response:
                    results.append(json.load(response))

            first = threading.Thread(target=first_request)
            try:
                first.start()
                self.assertTrue(StubYolo.started.wait(3))
                with self.assertRaises(HTTPError) as busy:
                    urlopen(Request(url, data=jpeg, headers={'X-Internal-Key': 'test-key'}), timeout=3)
                self.assertEqual(429, busy.exception.code)
                with urlopen(f'http://127.0.0.1:{adapter.server_port}/health', timeout=3) as health:
                    state = json.load(health)
                    self.assertEqual({'inFlight': 1, 'maxInFlight': 1},
                                     {key: state[key] for key in ('inFlight', 'maxInFlight')})
                StubYolo.release.set()
                first.join(5)
                self.assertEqual(3, results[0]['personCount'])
            finally:
                StubYolo.release.set()
                first.join(5)
                adapter.shutdown()
                upstream.shutdown()
                adapter.server_close()
                upstream.server_close()


if __name__ == '__main__':
    unittest.main()

import io
import json
from pathlib import Path
import sys
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parent))
from local_gateway import DeviceClient, Gateway, UsbDecoder, parse_rmc


def nmea(body):
    checksum = 0
    for byte in body.encode(): checksum ^= byte
    return '$' + body + '*' + format(checksum, '02X')


class UsbProtocolTest(unittest.TestCase):
    def test_valid_rmc_converts_knots_and_hemispheres(self):
        fix = parse_rmc(nmea('GNRMC,120000.000,A,3450.0000,S,05830.0000,W,10.0,90.0,031126,,,A'))
        self.assertTrue(fix['gpsValid'])
        self.assertAlmostEqual(fix['latitude'], -34.8333333333)
        self.assertEqual(fix['longitude'], -58.5)
        self.assertEqual(fix['speedKph'], 18.52)

    def test_invalid_fix_is_not_a_stationary_valid_fix(self):
        fix = parse_rmc(nmea('GNRMC,120000.000,V,,,,,,,031126,,,N'))
        self.assertFalse(fix['gpsValid'])
        self.assertIsNone(fix['latitude'])
        self.assertIsNone(fix['longitude'])

    def test_checksum_nonfinite_and_coordinate_bounds(self):
        valid = nmea('GPRMC,120000,A,3000.00,N,12000.00,E,0,0,031126,,,A')
        self.assertIsNone(parse_rmc(valid[:-2] + '00'))
        for lat, lng, speed in [('9060.0', '12000.0', '0'), ('9000.1', '12000.0', '0'), ('3000.0', '18000.1', '0'), ('3000.0', '12000.0', 'NaN'), ('3000.0', '12000.0', '100')]:
            self.assertIsNone(parse_rmc(nmea(f'GPRMC,120000,A,{lat},N,{lng},E,{speed},0,031126,,,A')))

    def test_split_binary_frames_do_not_parse_embedded_gps_markers(self):
        sentence = nmea('GPRMC,120000,A,3000.00,N,12000.00,E,0,0,031126,,,A')
        jpeg = b'\xff\xd8GPS_NMEA ' + sentence.encode() + b'\r\n\xff\xd9'
        wire = b'noise\r\nJPEG_TX_BEGIN LEN=' + str(len(jpeg)).encode() + b'\r\n' + jpeg + b'\r\nGPS_NMEA ' + sentence.encode() + b'\r\n'
        for chunk_size in [1, 3, 17, 8192]:
            decoder, received = UsbDecoder(), []
            for start in range(0, len(wire), chunk_size): received += decoder.feed(wire[start:start+chunk_size], 1000)
            self.assertEqual([row[0] for row in received], ['image', 'telemetry'])
            self.assertEqual(received[0][1], jpeg)

    def test_malformed_header_recovers_without_unbounded_buffer(self):
        decoder = UsbDecoder()
        decoder.feed(b'JPEG_TX_BEGIN LEN=99999999\r\n' + b'x' * 200000)
        self.assertLess(len(decoder.buffer), 100)
        sentence = nmea('GNRMC,120000,V,,,,,,,031126,,,N')
        events = decoder.feed(b'GPS_NMEA ' + sentence.encode() + b'\r\n')
        self.assertEqual(events[0][0], 'telemetry')


class CadenceTest(unittest.TestCase):
    def gateway(self):
        return Gateway(SimpleNamespace(controlled=False, telemetry_ms=3000, image_ms=30000, device_id='SG-TEST'), None)

    def test_usb_jitter_does_not_double_the_three_second_gps_period(self):
        gateway = self.gateway()
        with patch('local_gateway.time.monotonic', side_effect=[100.0, 102.99, 106.0]):
            for captured in [100000, 102990, 106000]:
                gateway.accept('telemetry', {'gpsValid': False}, captured)
        self.assertEqual(gateway.telemetry_queue.qsize(), 3)

    def test_camera_jitter_keeps_thirty_second_frames_and_rejects_fast_frames(self):
        gateway = self.gateway()
        received = []
        with patch('local_gateway.time.monotonic', side_effect=[100.0, 100.1, 104.0, 128.0, 129.85, 159.7]):
            for captured in [100000, 100100, 104000, 128000, 129850, 159700]:
                gateway.accept('image', b'\xff\xd8jpeg\xff\xd9', captured)
                if not gateway.image_queue.empty():received.append(gateway.image_queue.get_nowait())
        self.assertEqual([sample['capturedAtMs'] for sample, _ in received], [100000, 129850, 159700])

    def test_coarse_location_keeps_age_and_never_becomes_gps_speed(self):
        gateway = self.gateway()
        gateway.accept('lbs', dict(latitude=30.0, longitude=120.0, ageMs=20000), 100000)
        invalid = dict(gpsValid=False, latitude=None, longitude=None, speedKph=0, heading=None, gpsAccuracy=None)
        with patch('local_gateway.time.monotonic', side_effect=[100.0, 103.0, 106.0]):
            gateway.accept('telemetry', invalid, 101000)
            coarse = gateway.telemetry_queue.get_nowait()
            self.assertFalse(coarse['gpsValid']); self.assertEqual(coarse['locationSource'], 'LBS')
            self.assertEqual(coarse['locationAgeMs'], 21000); self.assertEqual(coarse['speedKph'], 0)
            gateway.accept('telemetry', dict(invalid, gpsValid=True, latitude=31.0, longitude=121.0, speedKph=12), 104000)
            gps = gateway.telemetry_queue.get_nowait()
            self.assertEqual(gps['locationSource'], 'GPS'); self.assertEqual(gps['speedKph'], 12)
            gateway.accept('telemetry', invalid, 800000)
            expired = gateway.telemetry_queue.get_nowait()
            self.assertEqual(expired['locationSource'], 'NONE'); self.assertIsNone(expired['latitude'])

    def test_cell_lines_are_validated_and_jpeg_payload_remains_opaque(self):
        line = b'CELL_LOCATION {"latitude":30.0,"longitude":120.0,"locationSource":"LBS","gpsValid":false,"ageMs":1000}\r\n'
        decoder = UsbDecoder()
        received=[]
        for byte in line: received += decoder.feed(bytes([byte]),10000)
        self.assertEqual(received, [('lbs',dict(latitude=30.0, longitude=120.0, ageMs=1000),10000)])
        self.assertEqual(UsbDecoder().feed(line.replace(b'30.0',b'NaN')), [])
        jpeg=b'\xff\xd8'+line+b'\xff\xd9'
        wire=b'JPEG_TX_BEGIN LEN='+str(len(jpeg)).encode()+b'\r\n'+jpeg
        self.assertEqual([x[0] for x in UsbDecoder().feed(wire)], ['image'])


class HttpProtocolTest(unittest.TestCase):
    def setUp(self):
        self.requests = []
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def do_POST(self):
                data = self.rfile.read(int(self.headers['Content-Length']))
                owner.requests.append((self.path, dict(self.headers), data))
                if self.path == '/redirect':
                    self.send_response(302); self.send_header('Location', '/should-not-receive-token'); self.end_headers(); return
                payload = json.dumps({'status': 'SAVED', 'imageId': 71, 'inferenceMode': 'disabled'} if self.path == '/saved' else {'code': 500, 'msg': '业务失败'} if self.path == '/business-error' else {'status': 'READY', 'mode': 'NORMAL', 'limitKph': 25, 'validUntilMs': 123456789, 'telemetryIntervalMs': 3000}).encode()
                self.send_response(200);self.send_header('Content-Length', str(len(payload)));self.end_headers();self.wfile.write(payload)
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True);self.thread.start()
        self.base = 'http://127.0.0.1:' + str(self.server.server_port)
        self.client = DeviceClient(self.base, 'a' * 64)
        self.sample = dict(deviceId='SG-TEST', sampleId='boot-1', capturedAtMs=123456)

    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join()

    def test_real_http_uses_official_json_header_and_multipart(self):
        self.client.telemetry(dict(self.sample, gpsValid=False, latitude=None, longitude=None, speedKph=0))
        self.client.image(self.sample, b'\xff\xd8payload\xff\xd9')
        self.assertEqual(self.requests[0][0], '/device/riderguard/telemetry')
        self.assertEqual(self.requests[0][1]['X-Device-Token'], 'a' * 64)
        self.assertEqual(json.loads(self.requests[0][2])['sampleId'], 'boot-1')
        path, headers, body = self.requests[1]
        self.assertEqual(path, '/device/riderguard/image')
        self.assertTrue(headers['Content-Type'].startswith('multipart/form-data; boundary='))
        for field in [b'name="deviceId"', b'name="sampleId"', b'name="capturedAtMs"', b'name="image"', b'\xff\xd8payload\xff\xd9']:
            self.assertIn(field, body)
        self.assertNotIn(('a' * 64).encode(), body)

    def test_token_is_not_forwarded_to_redirect_targets(self):
        client = DeviceClient(self.base, 'a' * 64, telemetry_url=self.base+'/redirect')
        with self.assertRaises(HTTPError): client.telemetry(self.sample)
        self.assertEqual(len(self.requests), 1)

    def test_saved_without_inference_is_acknowledged(self):
        client = DeviceClient(self.base, 'a' * 64, image_url=self.base + '/saved')
        response = client.image(self.sample, b'\xff\xd8payload\xff\xd9')
        self.assertEqual(response['status'], 'SAVED')
        self.assertNotIn('personCount', response)
        self.assertEqual(len(self.requests), 1)

    def test_http_200_business_errors_are_not_acknowledged_as_policy(self):
        client = DeviceClient(self.base, 'a' * 64, telemetry_url=self.base+'/business-error')
        with self.assertRaises(ValueError): client.telemetry(self.sample)

    def test_remote_plaintext_and_embedded_credentials_are_rejected(self):
        for url in ['http://192.0.2.1/device', 'https://user:pass@example.com/device', 'file:///tmp/test']:
            with self.assertRaises(ValueError): DeviceClient(url, 'a' * 64)


if __name__ == '__main__': unittest.main()

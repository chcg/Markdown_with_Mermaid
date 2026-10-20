"""Deterministic HTTP fixtures without CORS headers for native DOCX image tests."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import sys
import time
import threading

counts = {}
counts_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *_):
        pass

    def do_GET(self):
        route = self.path.split('?', 1)[0]
        with counts_lock:
            counts[self.path] = counts.get(self.path, 0) + 1
            request_count = counts[self.path]
            record = {'path': self.path, 'count': request_count, 'user_agent': self.headers.get('User-Agent'), 'cookie': self.headers.get('Cookie'),
                      'authorization': self.headers.get('Authorization')}
            with Path('build/image-http-requests.jsonl').open('a', encoding='utf-8') as output:
                output.write(json.dumps(record) + '\n')
        if route == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/redirected.svg')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if route == '/loop':
            self.send_response(302)
            self.send_header('Location', '/loop')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if route == '/oversize':
            self.send_response(200)
            self.send_header('Content-Type', 'image/png')
            self.send_header('Content-Length', str(16 * 1024 * 1024 + 1))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.close_connection = True
            return
        if route == '/missing':
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if route == '/once.svg' and request_count > 1:
            self.send_error(410, 'Image can be fetched only once')
            return
        if route == '/slow.svg':
            time.sleep(3)
        if route == '/html':
            body, mime = b'<!DOCTYPE html><html>This is not an image.</html>', 'text/html'
        else:
            label = route.strip('/').replace('.svg', '')
            body = (f'<svg xmlns="http://www.w3.org/2000/svg" width="144" height="32">'
                    f'<rect width="144" height="32" fill="#0969da"/>'
                    f'<text x="8" y="21" fill="white" font-size="13">{label}</text></svg>').encode()
            mime = 'image/svg+xml'
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


class ImageServer(ThreadingHTTPServer):
    request_queue_size = 64


if __name__ == '__main__':
    server = ImageServer(('127.0.0.1', 0), Handler)
    Path('build/image-server.json').write_text(json.dumps({'url': f'http://127.0.0.1:{server.server_port}'}), encoding='utf-8')
    print(f'Image test server on {server.server_port}', flush=True)
    server.serve_forever()

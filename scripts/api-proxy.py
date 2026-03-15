#!/usr/bin/env python3
"""
Local reverse proxy: HTTP -> HTTPS for QEMU ESP32-C3.

ESP32 connects to http://10.0.2.2:8888/v1/messages (plain HTTP, no TLS)
This proxy forwards to https://api.hiyo.top/v1/messages (TLS 1.3)

Usage:
    python3 scripts/api-proxy.py                          # default target
    python3 scripts/api-proxy.py https://api.hiyo.top     # custom target
"""

import http.server
import urllib.request
import ssl
import sys
import os

PORT = int(os.environ.get("PORT", sys.argv[2] if len(sys.argv) > 2 else "8888"))
TARGET = sys.argv[1] if len(sys.argv) > 1 else "https://api.hiyo.top"


class ProxyHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        url = TARGET + self.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length > 0 else b""

        headers = {
            "Content-Type": self.headers.get("Content-Type", "application/json"),
            "x-api-key": self.headers.get("x-api-key", ""),
            "anthropic-version": self.headers.get("anthropic-version", "2023-06-01"),
            "User-Agent": "curl/8.18.0",
        }

        req = urllib.request.Request(url, data=body, headers=headers, method="POST")
        ctx = ssl.create_default_context()

        try:
            with urllib.request.urlopen(req, context=ctx, timeout=120) as resp:
                resp_body = resp.read()
                self.send_response(resp.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp_body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(resp_body)
                print(f"[proxy] POST {self.path} -> {resp.status} ({len(resp_body)} bytes)")
        except urllib.error.HTTPError as e:
            resp_body = e.read()
            self.send_response(e.code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp_body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(resp_body)
            print(f"[proxy] POST {self.path} -> {e.code} ({len(resp_body)} bytes)")
        except Exception as e:
            msg = f'{{"error": "{e}"}}'.encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(msg)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(msg)
            print(f"[proxy] POST {self.path} -> 502 ({e})")

    def log_message(self, format, *args):
        pass  # Suppress default logging, we log in do_POST


if __name__ == "__main__":
    print(f"API proxy listening on 0.0.0.0:{PORT}")
    print(f"Target: {TARGET}")
    print(f"ESP32 config: API_URL = http://10.0.2.2:{PORT}/v1/messages")
    print()
    server = http.server.HTTPServer(("0.0.0.0", PORT), ProxyHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nProxy stopped.")

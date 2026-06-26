#!/usr/bin/env python3
"""Run the HttpClient header capture test against a local fake GitHub response."""

from http.server import BaseHTTPRequestHandler, HTTPServer
import subprocess
import sys
import threading


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):  # pylint: disable=invalid-name
        self.send_response(403)
        self.send_header("X-RateLimit-Remaining", "0")
        self.send_header("X-RateLimit-Reset", "1700000120")
        self.end_headers()
        self.wfile.write(b'{"message":"rate limited"}')

    def log_message(self, *_args):
        pass


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_http_client_headers_test.py <test-binary>", file=sys.stderr)
        return 2

    server = HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    try:
        url = f"http://127.0.0.1:{server.server_port}/repos/ggml-org/llama.cpp/releases/latest"
        return subprocess.run([sys.argv[1], url], check=False).returncode
    finally:
        server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())

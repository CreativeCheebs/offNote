"""Stand-in for connectors/affine-sidecar used by core_tests: records the POST it receives.

    python sidecar_stub.py 8799 recorded.json
    set OFFNOTE_TEST_SIDECAR=http://127.0.0.1:8799 && OffnoteQtTests.exe

It never talks to a real AFFiNE instance or sidecar.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

port, out_path = int(sys.argv[1]), sys.argv[2]


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8")
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump({"path": self.path, "auth": self.headers.get("Authorization"),
                       "content_type": self.headers.get("Content-Type"), "body": json.loads(body)}, f)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

    def log_message(self, *args):
        pass


HTTPServer(("127.0.0.1", port), Handler).serve_forever()

"""本机回环 HTTP 夹具；不下载公网、不构建 DLL、不写生产缓存。"""
import argparse
import http.server
import os
from pathlib import Path
import socket
import subprocess
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/slow":
            time.sleep(3)
        code = 200 if self.path == "/ok" else 502 if self.path == "/502" else 404
        self.send_response(code)
        self.send_header("Content-Length", "3")
        self.end_headers()
        try:
            self.wfile.write(b"SPR")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True, type=Path, help="包含本轮CI lua.exe/ggelua.dll的目录")
    args = parser.parse_args()
    engine = args.engine.resolve()
    with socket.socket() as closed:
        closed.bind(("127.0.0.1", 0))
        closed_port = closed.getsockname()[1]
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    env = os.environ.copy()
    env["MYGXY_CDN_ROUTE"] = f"http://127.0.0.1:{server.server_port}"
    try:
        result = subprocess.run([str(engine / "lua.exe"), str(Path(__file__).with_suffix(".lua")),
                                 str(engine), env["MYGXY_CDN_ROUTE"], str(closed_port)],
                                env=env, timeout=60, check=False)
    finally:
        server.shutdown()
        server.server_close()
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

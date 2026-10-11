#!/usr/bin/env python3
"""关节模组上位机 —— 静态文件 + 终端，**同一个端口**。

取代原来的 `python3 -m http.server 8080`：

    普通请求                    → 静态文件（行为对齐 http.server）
    /term + Upgrade: websocket  → 终端（PTY 桥）

用法：

    python3 serve.py                     # 0.0.0.0:8080
    python3 serve.py --port 8080 --bind 0.0.0.0

★ **不要用 sudo 起这个**。终端里开的 shell 就是当前用户的 shell；
  用 sudo 起就等于把 root shell 交出去。Qt 下位机那边要 sudo（EtherCAT 要
  raw socket），两个进程分开起就行。

纯标准库，零 pip 依赖 —— 试验台是离线环境，装不了包。
"""

import argparse
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from server import conn, pty_base, websocket   # noqa: E402

TERM_PATH = "/term"


class Handler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"       # 开 keep-alive：页面要拉一堆小文件

    def __init__(self, *args, **kwargs):
        # 固定以本文件所在目录为根 —— 无论从哪个 cwd 启动都一致
        super().__init__(*args, directory=HERE, **kwargs)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == TERM_PATH and websocket.is_websocket(self.headers):
            self._terminal()
            return
        super().do_GET()

    def _terminal(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(400, "missing Sec-WebSocket-Key")
            return
        remote = self.client_address[0]

        # 从这里开始这条连接归终端了：不能让它再回到 HTTP 的 keep-alive 循环
        self.close_connection = True
        self.connection.sendall(websocket.handshake_response(key))

        print(f"[term] {remote} 打开终端", flush=True)
        try:
            conn.serve_terminal(self.connection, self.rfile)
        finally:
            print(f"[term] {remote} 关闭终端", flush=True)

    def log_message(self, fmt, *args):
        # 静态请求不打日志（一个页面几十个文件，刷屏），终端那两行上面单独打
        pass


def main():
    ap = argparse.ArgumentParser(
        description="关节模组上位机：静态文件 + 终端（同端口）")
    ap.add_argument("--port", type=int, default=8080, help="监听端口（默认 8080）")
    ap.add_argument("--bind", default="0.0.0.0",
                    help="监听地址（默认 0.0.0.0，同 python -m http.server）")
    a = ap.parse_args()

    if not pty_base.supported():
        print("警告：当前平台不支持终端，界面照常提供，终端页会显示不可用。",
              file=sys.stderr)

    httpd = ThreadingHTTPServer((a.bind, a.port), Handler)
    httpd.daemon_threads = True

    host = "localhost" if a.bind in ("0.0.0.0", "::") else a.bind
    print(f"界面      http://{host}:{a.port}/")
    print(f"终端      ws://{host}:{a.port}{TERM_PATH}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()

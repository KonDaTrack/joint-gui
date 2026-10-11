"""终端页的服务端（纯标准库）。

试验台是离线环境，装不了 pip 包，所以这里没有任何第三方依赖：

  websocket.py  手写 RFC 6455（标准库没有 WebSocket）
  pty_base.py   PTY 抽象 + 环境准备
  pty_unix.py   os.forkpty()
  conn.py       单条连接的生命周期

**只用 Unix 的 PTY**：`serve.py` 跑在板子（Linux）上，浏览器那侧只负责显示，
不需要 PTY。详见 pty_base.py 顶部那段说明。
"""

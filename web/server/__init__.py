"""终端页的服务端（纯标准库）。

试验台是离线环境，装不了 pip 包，所以这里没有任何第三方依赖：

  websocket.py  手写 RFC 6455（标准库没有 WebSocket）
  pty_base.py   PTY 抽象 + 按平台选实现
  pty_unix.py   os.forkpty()
  pty_win.py    ctypes 调 ConPTY
  conn.py       单条连接的生命周期

平台分叉**只**在 pty_unix / pty_win 两个文件里，两者接口完全一样。
这样 Windows 那段即使要来回调几轮，也只动一个文件。
"""

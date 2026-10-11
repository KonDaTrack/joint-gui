"""单条终端连接的生命周期。

每格 shell 两条线程（设计 §2.5）：

    输出泵：阻塞在 pty.read() 上，读到什么就往 WS 写什么
    主线程：阻塞在 ws.recv() 上，二进制→pty，文本→控制消息

**为什么不用 select**：Windows 的 ConPTY 给的是管道，而 Windows 的 select
只认 socket。两条线程各自阻塞，Unix 的 fd 和 Windows 的管道都成立 ——
平台就统一了，不用为 select 再套一层 self-pipe。代价是每格 2 条线程，
一屏几十格也才几十条。
"""

import json
import threading

from . import pty_base
from .websocket import OP_BIN, OP_TEXT, WSConnection

CHUNK = 32 << 10


def serve_terminal(sock, rfile):
    """把一条已经握过手的 socket 变成终端会话。返回时连接已结束。"""
    ws = WSConnection(sock, rfile)

    if not pty_base.supported():
        ws.send_text(json.dumps(
            {"t": "error", "message": "当前平台没有 PTY，终端不可用（需要 Unix 的 os.forkpty）"},
            ensure_ascii=False))
        ws.close(1011)
        ws.abort()
        return

    try:
        pty = pty_base.spawn([pty_base.default_shell()], pty_base.child_env(),
                             pty_base.DEFAULT_COLS, pty_base.DEFAULT_ROWS,
                             cwd=pty_base.default_cwd())
    except OSError as e:
        ws.send_text(json.dumps({"t": "error", "message": f"启动终端失败：{e}"},
                                ensure_ascii=False))
        ws.close(1011)
        ws.abort()
        return

    stop = threading.Event()

    def pump():
        """输出泵：pty → WS。"""
        exited = False
        try:
            while not stop.is_set():
                data = pty.read(CHUNK)
                if not data:
                    exited = True
                    break
                ws.send_binary(data)
        except OSError:
            pass
        finally:
            # shell 自己退出了（敲了 exit）→ 给前端一个明确的结局，
            # 而不是让连接毫无征兆地断掉
            if exited and not stop.is_set():
                ws.send_text(json.dumps({"t": "exit", "code": 0}))
                ws.close(1000)
            stop.set()
            ws.abort()      # 把阻塞在 recv 上的主线程叫醒

    threading.Thread(target=pump, name="term-pump", daemon=True).start()

    try:
        while not stop.is_set():
            msg = ws.recv()
            if msg is None:
                break                   # 前端关了窗格 / 刷新 / 断网
            op, payload = msg
            if op == OP_BIN:
                pty.write(payload)
            elif op == OP_TEXT:
                _control(pty, payload)
    except Exception:
        pass
    finally:
        stop.set()
        pty.close()     # ★ 收尸：SIGHUP 掉整个进程组 / 关 ConPTY。
                        #   漏了这句就会攒一堆僵尸 shell。
        ws.abort()


def _control(pty, payload):
    """文本帧 = 控制消息（二进制帧才是终端数据）。"""
    try:
        msg = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return
    if msg.get("t") != "resize":
        return
    try:
        cols = int(msg.get("cols", pty_base.DEFAULT_COLS))
        rows = int(msg.get("rows", pty_base.DEFAULT_ROWS))
    except (TypeError, ValueError):
        return
    # 夹一下：前端可能算出离谱的值（比如页面隐藏时那一下），别把 pty 撑爆
    pty.resize(max(2, min(500, cols)), max(2, min(300, rows)))

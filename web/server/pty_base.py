"""PTY 抽象层。

两条平台实现（pty_unix / pty_win）必须满足同一套语义：
    read(n) -> bytes     阻塞；b"" 表示对端结束
    write(data)
    resize(cols, rows)
    close()              幂等，且**必须**把子进程收干净

**刻意不暴露 fileno()。** Windows 的 ConPTY 给的是管道，而 Windows 的 select()
只认 socket —— 暴露 fd 会诱导出"在 Windows 上 select 管道"这种写不通的代码。
所有实现都用阻塞读 + 每条连接两条线程（见 conn.py），平台就统一了。
"""

import os
import sys

# 新连接开多大。前端连上后会立刻用 fit 出来的真实尺寸覆盖它。
DEFAULT_COLS = 100
DEFAULT_ROWS = 30


def _impl():
    if sys.platform == "win32":
        from . import pty_win
        return pty_win
    from . import pty_unix
    return pty_unix


def supported():
    """这个平台能不能开终端。

    Linux / macOS 恒为真；Windows 要看有没有 ConPTY（Win10 1809 / build 17763 起）。
    """
    return _impl().supported()


def spawn(argv, env, cols=DEFAULT_COLS, rows=DEFAULT_ROWS):
    return _impl().spawn(argv, env, cols, rows)


def default_shell():
    """默认开哪个 shell。

    Unix 认 $SHELL；Windows 用 %COMSPEC%（cmd.exe）——
    它是最不会缺席的那一个，比 powershell 更适合当兜底。
    """
    if sys.platform == "win32":
        return os.environ.get("COMSPEC") or "cmd.exe"
    return os.environ.get("SHELL") or "/bin/bash"


def child_env():
    """交给 shell 的环境。

    TERM 必须设对，否则上层的程序不认 256 色、也不认行编辑。
    LINES/COLUMNS 要**删掉**：它们是"上一次的尺寸"，留着会让 shell 用陈旧值，
    正确的窗口大小由 TIOCSWINSZ / ResizePseudoConsole 决定。
    """
    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["COLORTERM"] = "truecolor"
    env.pop("LINES", None)
    env.pop("COLUMNS", None)
    return env

"""PTY 抽象层。

只有一条实现：`pty_unix`（`os.forkpty()`）。

**曾经有过 Windows 分支**（用 ctypes 调 ConPTY）。部署形态定下来之后删掉了：
`serve.py` 跑在 ARM 板（Linux）上，Windows 那边只开浏览器，ConPTY 没有任何部署路径 ——
而那段代码在开发机上根本跑不到，也就无从验证。留着一块"看着能用、没人验过"的代码，
比删掉更危险。真要恢复，git 里有（提交 81e9614）。

接口：
    read(n) -> bytes     阻塞；b"" 表示对端结束
    write(data)
    resize(cols, rows)
    close()              幂等，且**必须**把子进程收干净

**刻意不暴露 fileno()。** 唯一的使用方式是"阻塞读 + 每条连接两条线程"（见 conn.py）。
不暴露 fd，就没人能在实现里悄悄引入 select —— 真到了要支持 Windows 管道的那天，
那个坑现在就已经绕开了。
"""

import os

# 新连接开多大。前端连上后会立刻用 fit 出来的真实尺寸覆盖它。
DEFAULT_COLS = 100
DEFAULT_ROWS = 30


def _unix():
    # 延迟 import：pty_unix 依赖 fcntl / termios（Unix 独有）。
    # 放到模块顶层会让 serve.py 在没有这些模块的平台上**直接起不来** ——
    # 而终端不可用的时候，静态服务本来应该照常提供（见 conn.py 的降级）。
    from . import pty_unix
    return pty_unix


def supported():
    """这个平台能不能开终端。"""
    return hasattr(os, "forkpty")


def spawn(argv, env, cols=DEFAULT_COLS, rows=DEFAULT_ROWS, cwd=None):
    return _unix().spawn(argv, env, cols, rows, cwd)


def default_shell():
    """默认开哪个 shell。"""
    return os.environ.get("SHELL") or "/bin/bash"


def default_cwd():
    """终端从哪个目录开始。

    家目录（板子上就是 /home/cat 之类），**不是 serve.py 所在的 web/** ——
    否则一开窗格就站在一堆代码里，还得先 cd 出来。取 $HOME 而不是写死路径，
    这样板子、工控机、开发机上行为都对。
    """
    try:
        return os.path.expanduser("~") or None
    except Exception:
        return None


def child_env():
    """交给 shell 的环境。

    TERM 必须设对，否则上层的程序不认 256 色、也不认行编辑。
    LINES/COLUMNS 要**删掉**：它们是"上一次的尺寸"，留着会让 shell 用陈旧值 ——
    正确的窗口大小由 TIOCSWINSZ 决定。
    """
    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["COLORTERM"] = "truecolor"
    env.pop("LINES", None)
    env.pop("COLUMNS", None)
    return env

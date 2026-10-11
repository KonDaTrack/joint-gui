"""Unix 侧 PTY —— os.forkpty()。

为什么不用 pty.spawn()：它会把整个进程接管掉（自己进阻塞循环，直到子进程退出），
而我们要的是**把 master fd 交回来自己管**（两条线程，见 conn.py）。

为什么用 forkpty 而不是 openpty + subprocess：forkpty 在子进程里替我们做了
setsid + TIOCSCTTY + dup2 —— 少了 TIOCSCTTY 就没有控制终端，Ctrl+C / Ctrl+Z
这些作业控制会失效。而 subprocess 没法在子进程里做 ioctl（除非 preexec_fn，那个更不安全）。
"""

import errno
import fcntl
import os
import shutil
import signal
import struct
import termios
import time
import warnings


class UnixPty:
    def __init__(self, pid, master):
        self.pid = pid
        self.master = master
        self._closed = False

    # -------------------------------------------------- 读写

    def read(self, n):
        try:
            return os.read(self.master, n)
        except OSError as e:
            # 子进程一退出，master 上再读就是 EIO —— 这是正常的"结束"，不是错误
            if e.errno in (errno.EIO, errno.EBADF):
                return b""
            raise

    def write(self, data):
        if not data:
            return
        try:
            os.write(self.master, data)
        except OSError:
            pass        # 子进程没了就当写丢，别把输出泵线程炸掉

    def resize(self, cols, rows):
        try:
            fcntl.ioctl(
                self.master,
                termios.TIOCSWINSZ,
                struct.pack("HHHH", rows, cols, 0, 0),
            )
        except OSError:
            pass

    # -------------------------------------------------- 收尾

    def close(self):
        """幂等。先 SIGHUP 整个进程组，等它退出，再关 fd。

        顺序不能反：先关 master 的话，阻塞在 read 上的线程可能醒不过来。
        而 SIGHUP 让 shell 退出 → master 上读到 EIO → 泵线程自己就结束了。
        """
        if self._closed:
            return
        self._closed = True
        self._terminate()
        try:
            os.close(self.master)
        except OSError:
            pass

    def _terminate(self):
        # 杀**整个进程组**而不是单个 pid：shell 里可能还挂着 ssh、top 之类
        # 它自己开的子进程，只杀 leader 会把它们留成孤儿。
        try:
            os.killpg(self.pid, signal.SIGHUP)
        except OSError:
            return              # 组已经没了，或者还没 setsid 完

        for _ in range(20):     # 最多等 1 秒
            try:
                done, _ = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                return          # 已经收过了
            if done:
                return
            time.sleep(0.05)

        try:
            os.killpg(self.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)     # 阻塞收尸，宁可等一下也不留僵尸
        except OSError:
            pass


def supported():
    return True


def spawn(argv, env, cols, rows, cwd=None):
    # 可执行文件路径**在父进程里**解析好。fork 之后子进程只做两个系统调用 ——
    # 多线程进程里 fork 出来的子进程越少碰 Python 越好（否则可能撞上别的线程
    # 正持有的锁）。连 PATH 查找都挪到父进程，就是这个道理。
    exe = shutil.which(argv[0]) or argv[0]
    if cwd and not os.path.isdir(cwd):
        cwd = None              # 目录不存在就当没给，别让子进程 chdir 失败

    with warnings.catch_warnings():
        # Python 3.12 起，多线程进程里调 fork 会报 DeprecationWarning。
        # 这个告警针对的是"fork 完还在子进程里跑 Python"的用法；
        # 我们 fork 完立刻 execve，是 forkpty 被设计出来就是干的那个用法。
        warnings.simplefilter("ignore", DeprecationWarning)
        pid, master = os.forkpty()

    if pid == 0:
        # ── 子进程 ──
        try:
            if cwd:
                os.chdir(cwd)
            os.execve(exe, [exe] + list(argv[1:]), env)
        except BaseException:
            os._exit(127)       # 用 _exit：不走 atexit、不冲刷父进程的缓冲

    p = UnixPty(pid, master)
    p.resize(cols, rows)        # forkpty 不带尺寸参数，起来先按前端给的设一次
    return p

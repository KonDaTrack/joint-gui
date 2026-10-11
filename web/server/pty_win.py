"""Windows 侧 PTY —— 用 ctypes 直接调 ConPTY。

Python 的 pty 模块是 Unix 独有的（openpty / forkpty 在 Windows 上根本没有），
等价物是 ConPTY（Windows 10 1809 / build 17763 起）。标准库没做封装，
所以这个文件里全是原始 Win32 调用。

    CreatePipe ×2 → CreatePseudoConsole → STARTUPINFOEXW + 属性表 → CreateProcessW

两条管道各自的"另一个方向"是给 ConPTY 的，父进程只留自己那一端：

    hInWrite  ──我写──▶ [管道] ──▶ hInRead   交给 ConPTY
    hOutRead  ◀──我读── [管道] ◀── hOutWrite 交给 ConPTY

★ 这个文件**在 Linux 上写得了、测不了**。接口和 pty_unix 完全一样，
   所以它如果出错，只改这一个文件就能验证，不会牵动别处。
"""

import ctypes
import os
import subprocess
import sys
from ctypes import wintypes

# ---------------------------------------------------------------- 常量

EXTENDED_STARTUPINFO_PRESENT = 0x00080000
CREATE_UNICODE_ENVIRONMENT = 0x00000400
PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE = 0x00020016
INFINITE = 0xFFFFFFFF

_k32 = None


# ---------------------------------------------------------------- 结构

class COORD(ctypes.Structure):
    _fields_ = [("X", ctypes.c_short), ("Y", ctypes.c_short)]


class STARTUPINFOEXW(ctypes.Structure):
    _fields_ = [
        ("StartupInfo", wintypes.STARTUPINFOW),
        ("lpAttributeList", ctypes.c_void_p),
    ]


# ---------------------------------------------------------------- API

def _bind():
    """挂上函数原型。只在 Windows 上真正跑得到。"""
    global _k32
    if _k32 is not None:
        return _k32
    k = ctypes.WinDLL("kernel32", use_last_error=True)

    k.CreatePipe.argtypes = [ctypes.POINTER(wintypes.HANDLE),
                             ctypes.POINTER(wintypes.HANDLE),
                             ctypes.c_void_p, wintypes.DWORD]
    k.CreatePipe.restype = wintypes.BOOL

    k.CreatePseudoConsole.argtypes = [COORD, wintypes.HANDLE, wintypes.HANDLE,
                                      wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    k.CreatePseudoConsole.restype = ctypes.c_long        # HRESULT

    k.ResizePseudoConsole.argtypes = [wintypes.HANDLE, COORD]
    k.ResizePseudoConsole.restype = ctypes.c_long

    k.ClosePseudoConsole.argtypes = [wintypes.HANDLE]

    k.InitializeProcThreadAttributeList.argtypes = [
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
        ctypes.POINTER(ctypes.c_size_t)]
    k.InitializeProcThreadAttributeList.restype = wintypes.BOOL

    k.UpdateProcThreadAttribute.argtypes = [
        ctypes.c_void_p, wintypes.DWORD, ctypes.c_size_t, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p]
    k.UpdateProcThreadAttribute.restype = wintypes.BOOL

    k.DeleteProcThreadAttributeList.argtypes = [ctypes.c_void_p]

    k.CreateProcessW.argtypes = [
        wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
        wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
        ctypes.c_void_p, ctypes.c_void_p]
    k.CreateProcessW.restype = wintypes.BOOL

    k.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                           ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    k.ReadFile.restype = wintypes.BOOL
    k.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    k.WriteFile.restype = wintypes.BOOL

    k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    k.WaitForSingleObject.restype = wintypes.DWORD
    k.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    k.CloseHandle.argtypes = [wintypes.HANDLE]

    _k32 = k
    return k


def supported():
    if sys.platform != "win32":
        return False
    try:
        v = sys.getwindowsversion()
    except Exception:
        return False
    return (v.major, v.build) >= (10, 17763)


def _env_block(env):
    """Windows 要一个排序、双 NUL 结尾的 Unicode 环境块。"""
    items = sorted((f"{k}={v}" for k, v in env.items()
                    if k and "=" not in k),
                   key=lambda s: s.split("=", 1)[0].upper())
    return ("\0".join(items) + "\0\0").encode("utf-16-le")


# ---------------------------------------------------------------- 连接

class WinPty:
    def __init__(self, proc, hpc, h_in_write, h_out_read):
        self.proc = proc
        self.hpc = hpc
        self.h_in = h_in_write
        self.h_out = h_out_read
        self._closed = False

    def read(self, n):
        buf = ctypes.create_string_buffer(n)
        got = wintypes.DWORD(0)
        try:
            ok = self.k.ReadFile(self.h_out, buf, n, ctypes.byref(got), None)
        except Exception:
            return b""
        if not ok or got.value == 0:
            return b""          # 管道断了 = 结束（同 Unix 的 EIO）
        return buf.raw[:got.value]

    def write(self, data):
        if not data:
            return
        try:
            self.k.WriteFile(self.h_in, data, len(data),
                             ctypes.byref(wintypes.DWORD(0)), None)
        except Exception:
            pass

    def resize(self, cols, rows):
        try:
            self.k.ResizePseudoConsole(self.hpc, COORD(cols, rows))
        except Exception:
            pass

    def close(self):
        """幂等。先关 HPCON（客户端会收到 CTRL_CLOSE_EVENT 并退出），
        再等进程真的死掉，最后关句柄。"""
        if self._closed:
            return
        self._closed = True
        k = self.k
        try:
            k.ClosePseudoConsole(self.hpc)
        except Exception:
            pass
        try:
            if k.WaitForSingleObject(self.proc, 3000) != 0:
                k.TerminateProcess(self.proc, 0)
                k.WaitForSingleObject(self.proc, 1000)
        except Exception:
            pass
        for h in (self.proc, self.h_in, self.h_out):
            try:
                k.CloseHandle(h)
            except Exception:
                pass


def spawn(argv, env, cols, rows):
    k = _bind()

    class SECURITY_ATTRIBUTES(ctypes.Structure):
        _fields_ = [("nLength", wintypes.DWORD),
                    ("lpSecurityDescriptor", ctypes.c_void_p),
                    ("bInheritHandle", wintypes.BOOL)]

    sa = SECURITY_ATTRIBUTES()
    sa.nLength = ctypes.sizeof(sa)
    sa.bInheritHandle = True

    h_in_read = wintypes.HANDLE()
    h_in_write = wintypes.HANDLE()
    h_out_read = wintypes.HANDLE()
    h_out_write = wintypes.HANDLE()
    if not k.CreatePipe(ctypes.byref(h_in_read), ctypes.byref(h_in_write),
                        ctypes.byref(sa), 0):
        raise OSError(ctypes.get_last_error(), "CreatePipe 失败")
    if not k.CreatePipe(ctypes.byref(h_out_read), ctypes.byref(h_out_write),
                        ctypes.byref(sa), 0):
        raise OSError(ctypes.get_last_error(), "CreatePipe 失败")

    hpc = wintypes.HANDLE()
    hr = k.CreatePseudoConsole(COORD(max(2, cols), max(2, rows)),
                               h_in_read, h_out_write, 0, ctypes.byref(hpc))
    if hr != 0:
        raise OSError(f"CreatePseudoConsole 失败：HRESULT 0x{hr & 0xFFFFFFFF:08X}")

    # 这两端归 ConPTY 了，父进程不要再留着
    k.CloseHandle(h_in_read)
    k.CloseHandle(h_out_write)

    # 属性表里挂上 HPCON —— 这是 ConPTY 和普通管道启动唯一的差别
    size = ctypes.c_size_t(0)
    k.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
    attrs = ctypes.create_string_buffer(max(size.value, 1))
    if not k.InitializeProcThreadAttributeList(attrs, 1, 0, ctypes.byref(size)):
        raise OSError(ctypes.get_last_error(), "InitializeProcThreadAttributeList 失败")
    try:
        if not k.UpdateProcThreadAttribute(
                attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                ctypes.byref(hpc), ctypes.sizeof(wintypes.HANDLE), None, None):
            raise OSError(ctypes.get_last_error(), "UpdateProcThreadAttribute 失败")

        si = STARTUPINFOEXW()
        si.StartupInfo.cb = ctypes.sizeof(STARTUPINFOEXW)
        si.lpAttributeList = ctypes.cast(attrs, ctypes.c_void_p)

        # CreateProcessW 会就地改命令行缓冲，所以必须是可写的
        cmd = ctypes.create_unicode_buffer(subprocess.list2cmdline(argv))
        envb = ctypes.create_string_buffer(_env_block(env))
        pi = PROCESS_INFORMATION_HOLDER()

        ok = k.CreateProcessW(
            None, cmd, None, None, True,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
            envb, None, ctypes.byref(si), pi.ptr())
        if not ok:
            raise OSError(ctypes.get_last_error(), "CreateProcessW 失败")
    finally:
        k.DeleteProcThreadAttributeList(attrs)

    k.CloseHandle(pi.hThread)

    p = WinPty(pi.hProcess, hpc, h_in_write, h_out_read)
    p.k = k
    return p


class PROCESS_INFORMATION_HOLDER:
    """PROCESS_INFORMATION 的一层薄封装（只为了让上面那段读起来干净些）。"""

    def __init__(self):
        class PROCESS_INFORMATION(ctypes.Structure):
            _fields_ = [("hProcess", wintypes.HANDLE),
                        ("hThread", wintypes.HANDLE),
                        ("dwProcessId", wintypes.DWORD),
                        ("dwThreadId", wintypes.DWORD)]
        self._pi = PROCESS_INFORMATION()

    def ptr(self):
        return ctypes.byref(self._pi)

    def __getattr__(self, name):
        return getattr(self._pi, name)

"""RFC 6455 WebSocket —— 手写实现（只做服务端）。

标准库没有 WebSocket，而试验台是离线环境装不了 pip 包，所以自己写。
**刻意做窄**，只覆盖终端这一条通道需要的部分：

  - 只接受掩码帧（RFC 6455 规定客户端必须掩码，服务端必须不掩码）
  - 支持续帧（continuation）拼接
  - 消化 ping→pong、忽略 pong、close 时回一个 close
  - **不协商任何扩展** —— 不回 Sec-WebSocket-Extensions 即可。
    permessage-deflate 对 PTY 这种本就零散的字节流没意义，只多一个出事的面
  - 载荷**一律按字节搬运**，绝不当文本解码 —— PTY 吐出来的可能是非法 UTF-8

消息大小有上限（MAX_MESSAGE），超了直接断开：免得对面一个坏帧把内存吃光。
"""

import base64
import hashlib
import socket
import struct
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_CONT = 0x0
OP_TEXT = 0x1
OP_BIN = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA

MAX_FRAME = 1 << 20        # 单帧上限 1MB
MAX_MESSAGE = 8 << 20      # 拼起来之后的整条消息上限 8MB
CHUNK = 32 << 10           # 发送时的分片粒度：帧小则持锁时间短，别把另一端饿死


# ---------------------------------------------------------------- 握手

def is_websocket(headers):
    """请求头看起来是不是一个 WebSocket 升级请求。"""
    up = (headers.get("Upgrade") or "").strip().lower()
    conn = (headers.get("Connection") or "").lower()
    return up == "websocket" and "upgrade" in conn


def handshake_response(client_key):
    """101 响应报文。原样回显 Sec-WebSocket-Accept，不回扩展头。"""
    accept = base64.b64encode(
        hashlib.sha1((client_key + GUID).encode("ascii")).digest()
    ).decode("ascii")
    return (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n"
    ).encode("ascii")


def _unmask(data, mask):
    """按 4 字节掩码解掩。

    逐字节的 Python 循环在 64KB 帧上要跑几毫秒（终端打字会明显发滞），
    所以用大整数整体异或 —— 转成 int 之后异或是 C 速度的。
    """
    n = len(data)
    if not n:
        return b""
    key = (mask * ((n + 3) // 4))[:n]
    return (int.from_bytes(data, "big") ^ int.from_bytes(key, "big")).to_bytes(n, "big")


# ---------------------------------------------------------------- 连接

class WSConnection:
    """一条 WebSocket 连接。

    读只由一条线程做（conn.py 的主线程），写可能来自两条线程（输出泵 + pong），
    所以**写侧加锁**。
    """

    def __init__(self, sock, rfile):
        self.sock = sock
        self.rfile = rfile          # 用 http.server 的 rfile：
                                    # 它可能已经把握手之后的数据读进缓冲区了，
                                    # 另建一个 makefile 会把这部分字节丢掉
        self._wlock = threading.Lock()
        self._closed = False

    # -------------------------------------------------- 读

    def _read_exact(self, n):
        buf = self.rfile.read(n)
        if buf is None or len(buf) < n:
            raise ConnectionError("连接已断开")
        return buf

    def _read_frame(self):
        b0, b1 = self._read_exact(2)
        if b0 & 0x70:
            raise ConnectionError("RSV 位非零（本实现不协商扩展）")
        fin = bool(b0 & 0x80)
        opcode = b0 & 0x0F

        masked = bool(b1 & 0x80)
        ln = b1 & 0x7F
        if ln == 126:
            ln = struct.unpack(">H", self._read_exact(2))[0]
        elif ln == 127:
            ln = struct.unpack(">Q", self._read_exact(8))[0]
        if ln > MAX_FRAME:
            raise ConnectionError("帧过大")

        mask = self._read_exact(4) if masked else None
        if mask is None:
            # RFC 6455：客户端发来的帧**必须**掩码
            raise ConnectionError("客户端帧未掩码")

        payload = self._read_exact(ln) if ln else b""
        if ln:
            payload = _unmask(payload, mask)
        return fin, opcode, payload

    def recv(self):
        """返回 (opcode, payload) 的**完整消息**；连接结束返回 None。

        ping / pong 在这里内部消化掉，不往上冒。
        """
        frag = bytearray()
        frag_op = None
        while True:
            try:
                fin, opcode, payload = self._read_frame()
            except (ConnectionError, OSError, ValueError):
                return None

            if opcode == OP_PING:
                self.send(OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                continue
            if opcode == OP_CLOSE:
                self.close(1000)
                return None

            if opcode == OP_CONT:
                if frag_op is None:
                    return None         # 没开头就来续帧 —— 协议错，直接断
                frag += payload
            else:
                if frag_op is not None:
                    return None         # 上一组还没拼完又来新的一组
                frag_op = opcode
                frag = bytearray(payload)

            if len(frag) > MAX_MESSAGE:
                self.close(1009)        # 1009 = message too big
                return None
            if fin:
                return frag_op, bytes(frag)

    # -------------------------------------------------- 写

    def _write_frame(self, fin, opcode, payload):
        n = len(payload)
        b0 = (0x80 if fin else 0x00) | opcode
        if n < 126:
            header = struct.pack("!BB", b0, n)
        elif n < 65536:
            header = struct.pack("!BBH", b0, 126, n)
        else:
            header = struct.pack("!BBQ", b0, 127, n)
        # 服务端 → 客户端：不加掩码
        self.sock.sendall(header + payload)

    def send(self, opcode, payload=b""):
        """线程安全。超过 CHUNK 的载荷自动分片。"""
        with self._wlock:
            if self._closed:
                return
            n = len(payload)
            if n <= CHUNK:
                self._write_frame(True, opcode, payload)
                return
            for i in range(0, n, CHUNK):
                part = payload[i:i + CHUNK]
                self._write_frame(
                    i + CHUNK >= n,                  # fin
                    opcode if i == 0 else OP_CONT,   # 首片带原 opcode，其余是续帧
                    part,
                )

    def send_text(self, s):
        self.send(OP_TEXT, s.encode("utf-8"))

    def send_binary(self, b):
        self.send(OP_BIN, b)

    def close(self, code=1000):
        """发一个 close 帧。之后所有 send 变成空操作。"""
        with self._wlock:
            if self._closed:
                return
            self._closed = True
            try:
                self._write_frame(True, OP_CLOSE, struct.pack("!H", code))
            except OSError:
                pass

    def abort(self):
        """从**另一条线程**把阻塞在 recv 上的那条叫醒。

        shutdown 之前的 FIN 排在已写数据之后，所以 close 帧不会丢。
        """
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

// 与下位机（Qt 端 ControlServer）的 WebSocket 连接层。
//
// 职责边界：这里只管"连接与收发"，不碰界面。
// 安全逻辑（行程限位、看门狗、控制权校验）一律在 Qt 端强制，
// 这里做的任何判断都只是"界面表现"，不能替代——见 docs/host-embedded-architecture.md。

const HEARTBEAT_MS = 200;      // 与 Qt 端 kRemoteHeartbeatTimeoutMs(1000) 匹配，留 5 倍余量
const RECONNECT_MS = 2000;

class JointLink {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.connected = false;
    this.heartbeatTimer = null;
    this.reconnectTimer = null;
    this.handlers = {};          // {type: fn}
  }

  on(type, fn) { this.handlers[type] = fn; return this; }

  _emit(type, msg) {
    if (this.handlers[type]) this.handlers[type](msg);
  }

  connect() {
    clearTimeout(this.reconnectTimer);
    try {
      this.ws = new WebSocket(this.url);
    } catch (e) {
      this._scheduleReconnect();
      return;
    }

    this.ws.addEventListener('open', () => {
      this.connected = true;
      this._emit('open', {});
    });

    this.ws.addEventListener('message', (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch { return; }
      this._emit(msg.type, msg);
      this._emit('*', msg);
    });

    this.ws.addEventListener('close', () => {
      this.connected = false;
      this._syncHeartbeat();      // connected 已置 false → 会停掉心跳
      this._emit('close', {});
      this._scheduleReconnect();
    });

    // error 之后浏览器一定会触发 close，重连交给 close 处理，避免重复
    this.ws.addEventListener('error', () => {});
  }

  _scheduleReconnect() {
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = setTimeout(() => this.connect(), RECONNECT_MS);
  }

  _syncHeartbeat() {
    clearInterval(this.heartbeatTimer);
    this.heartbeatTimer = null;
    // 连着就一直发。心跳是**下位机看门狗唯一的输入**：
    // 它超时（1s）就失能所有轴 —— 因为下位机现在没有本地界面，
    // 网断了/这个页面卡死了，没人发得出停止命令。
    // ★ 所以这里**不能**有条件（以前是"只在持有控制权时才发"）：
    //   一旦不发，看门狗就永远武装不起来，等于把人不在时自停的保护废掉。
    if (this.connected) {
      this.heartbeatTimer = setInterval(() => this.send({ type: 'heartbeat' }), HEARTBEAT_MS);
    }
  }

  send(obj) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(obj));
    }
  }

  command(name, args = {}) { this.send({ type: 'command', name, args }); }
}

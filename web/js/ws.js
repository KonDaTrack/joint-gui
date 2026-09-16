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
    this.holding = false;        // 是否已拿到控制权（决定要不要发心跳）
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
      // 控制权是否还在自己手上，决定心跳的启停
      if (msg.type === 'controlOwner') {
        this.holding = (msg.owner === 'remote');
        this._syncHeartbeat();
      }
      this._emit(msg.type, msg);
      this._emit('*', msg);
    });

    this.ws.addEventListener('close', () => {
      this.connected = false;
      this.holding = false;
      this._syncHeartbeat();
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
    // 只在持有控制权时发心跳：下位机超时（1s）会自动收回并失能，
    // 不发心跳就不会一直占着控制权
    if (this.holding && this.connected) {
      this.heartbeatTimer = setInterval(() => this.send({ type: 'heartbeat' }), HEARTBEAT_MS);
    }
  }

  send(obj) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(obj));
    }
  }

  requestControl() { this.send({ type: 'requestControl' }); }
  releaseControl() { this.send({ type: 'releaseControl' }); }
  command(name, args = {}) { this.send({ type: 'command', name, args }); }
}

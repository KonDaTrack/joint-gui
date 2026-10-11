#!/usr/bin/env node
// 终端后端对拍脚本。
//
// 手写 WebSocket 最容易翻车的地方是分片、掩码和大帧 —— 这个脚本专门撞它们。
// 用 Node 24 的原生 WebSocket（仓库里 test-client.js 已有先例），零依赖。
//
// 跑法：node tests/term_client.js
// 它会自己起一个 serve.py（临时端口、临时 HOME，避免被用户的 .bashrc 干扰），
// 跑完自己收掉。

const { spawn, execSync } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const PORT = Number(process.env.TERM_TEST_PORT || 18080);
const WEB = path.resolve(__dirname, '..');
const URL_TERM = `ws://127.0.0.1:${PORT}/term`;

let pass = 0, fail = 0;
const ok = (name, cond, extra = '') => {
  if (cond) { pass++; console.log(`  \x1b[32m✓\x1b[0m ${name}`); }
  else { fail++; console.log(`  \x1b[31m✗\x1b[0m ${name}${extra ? '  ' + extra : ''}`); }
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------- 起服务

async function waitPort(port, timeoutMs = 8000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const okc = await new Promise((res) => {
      const s = net.connect(port, '127.0.0.1');
      s.on('connect', () => { s.destroy(); res(true); });
      s.on('error', () => res(false));
    });
    if (okc) return true;
    await sleep(100);
  }
  return false;
}

// ---------------------------------------------------------------- 连接

function openTerm(url = URL_TERM) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    ws.buf = Buffer.alloc(0);
    ws.onmessage = (e) => {
      const b = Buffer.isBuffer(e.data) ? e.data : Buffer.from(e.data);
      ws.buf = Buffer.concat([ws.buf, b]);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error('WebSocket 连接失败'));
    ws.onclose = () => { ws.closed = true; };
  });
}

/** 等到缓冲区里出现 needle（支持字符串或正则），超时抛错。 */
async function waitFor(ws, needle, timeoutMs = 8000) {
  const t0 = Date.now();
  const hit = () => typeof needle === 'string'
    ? ws.buf.includes(needle)
    : needle.test(ws.buf.toString('latin1'));
  while (Date.now() - t0 < timeoutMs) {
    if (hit()) return true;
    if (ws.closed) throw new Error('连接提前关闭');
    await sleep(25);
  }
  throw new Error(`超时：等不到 ${needle}`);
}

// ★ 按键走**二进制帧**，不是文本帧。
//   文本帧专供控制消息（resize 之类），一旦让按键也走文本，
//   用户在命令行里敲出 {"t":"resize",...} 就会被当成控制消息吞掉。
const enc = new TextEncoder();
const send = (ws, s) => ws.send(enc.encode(s));
const sendLine = (ws, s) => ws.send(enc.encode(s + '\n'));
const resize = (ws, cols, rows) => ws.send(JSON.stringify({ t: 'resize', cols, rows }));

// ---------------------------------------------------------------- 测试

async function main() {
  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'termtest-'));
  const srv = spawn('python3', [path.join(WEB, 'serve.py'), '--port', String(PORT), '--bind', '127.0.0.1'], {
    cwd: WEB,
    env: { ...process.env, HOME: home, SHELL: '/bin/bash', TERM: 'dumb' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let srvOut = '';
  srv.stdout.on('data', (d) => { srvOut += d; });
  srv.stderr.on('data', (d) => { srvOut += d; });

  const cleanup = () => {
    try { srv.kill('SIGKILL'); } catch {}
    try { fs.rmSync(home, { recursive: true, force: true }); } catch {}
  };

  try {
    if (!await waitPort(PORT)) throw new Error('serve.py 没起来：\n' + srvOut);

    // ---- 1. 回显 ----------------------------------------------------
    console.log('\n[1] 基本回显');
    let ws = await openTerm();
    sendLine(ws, "echo HELLO_$((6*7))");
    await waitFor(ws, 'HELLO_42');
    ok('命令执行结果回显', true);
    ws.close();
    await sleep(300);

    // ---- 2. resize ---------------------------------------------------
    console.log('\n[2] resize → stty size');
    ws = await openTerm();
    resize(ws, 132, 43);
    await sleep(200);
    sendLine(ws, 'stty size');
    await waitFor(ws, /43\s+132/);
    ok('resize 真的传到了 pty（43 132）', true);
    ws.close();
    await sleep(300);

    // ---- 3. 大输出 / 分片 ---------------------------------------------
    // head -c 200000 /dev/zero | tr '\0' X  → 正好 200000 个可打印字符、无换行，
    // 于是"收到多少字节"可以有精确期望值。PTY 写满会阻塞而不是丢数据，
    // 所以这里对不上就说明是 WS 层丢帧了。
    console.log('\n[3] 大输出（200000 字节，撞 WS 分片与大帧）');
    ws = await openTerm();
    // ★ 标记必须在**回显里不出现**，否则 waitFor 一看到被回显的命令行就返回了，
    //   那时真正的输出还没开始（这个坑当场踩过一次）。
    //   所以命令行里写的是拼装式 '__W%s__' B，字面量 __WB__ 只可能来自输出。
    const B = '__WB__', E = '__WE__';
    sendLine(ws, `printf '__W%s__' B; head -c 200000 /dev/zero | tr '\\0' X; printf '__W%s__' E`);
    await waitFor(ws, E, 20000);
    const s = ws.buf.toString('latin1');
    const i = s.lastIndexOf(B), j = s.indexOf(E, i);
    const got = j - i - B.length;
    ok('200000 字节一字不差', got === 200000, `实收 ${got}`);
    ws.close();
    await sleep(300);

    // ---- 4. 路径限定 -------------------------------------------------
    console.log('\n[4] 非 /term 的升级请求被拒');
    const raw = await rawUpgrade('/nope');
    ok('回的不是 101', !/^HTTP\/1\.1 101/.test(raw), raw.split('\r\n')[0]);
    const raw2 = await rawUpgrade('/term');
    ok('/term 正常升级', /^HTTP\/1\.1 101/.test(raw2), raw2.split('\r\n')[0]);
    await sleep(300);

    // ---- 5. 收尸 -----------------------------------------------------
    console.log('\n[5] 关连接后不留僵尸 shell');
    const before = childCount(srv.pid);
    const many = [];
    for (let k = 0; k < 5; k++) {
      const w = await openTerm();
      sendLine(w, 'echo READY');
      await waitFor(w, 'READY');
      many.push(w);
    }
    await sleep(400);
    const during = childCount(srv.pid);
    ok('5 条连接 → 5 个 shell', during - before === 5, `before=${before} during=${during}`);
    many.forEach((w) => w.close());
    await sleep(1500);
    const after = childCount(srv.pid);
    ok('关掉后全部回收', after === before, `after=${after}`);

  } finally {
    cleanup();
  }

  console.log(`\n${fail === 0 ? '\x1b[32m全部通过\x1b[0m' : '\x1b[31m有失败\x1b[0m'}：${pass} 通过 / ${fail} 失败\n`);
  process.exit(fail === 0 ? 0 : 1);
}

/** 数某个进程的直接子进程（我们的 shell 都挂在 serve.py 底下）。 */
function childCount(pid) {
  try {
    const out = execSync(`pgrep -P ${pid} || true`, { encoding: 'utf8' }).trim();
    return out ? out.split('\n').length : 0;
  } catch { return 0; }
}

/** 手工发一个带 Upgrade 头的 GET，用来验路径限定。 */
function rawUpgrade(p) {
  return new Promise((resolve) => {
    const s = net.connect(PORT, '127.0.0.1', () => {
      s.write(`GET ${p} HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n`
            + `Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n`
            + `Sec-WebSocket-Version: 13\r\n\r\n`);
    });
    let d = '';
    s.on('data', (c) => { d += c.toString('latin1'); });
    s.on('end', () => resolve(d));
    s.on('error', () => resolve(d));
    setTimeout(() => { s.destroy(); resolve(d); }, 1200);
  });
}

main().catch((e) => { console.error('\x1b[31m测试异常：\x1b[0m', e.message); process.exit(1); });

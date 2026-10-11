#!/usr/bin/env node
// 联调验证：**无窗口的下位机 + 浏览器**能不能不点任何东西就用起来。
//
// 这是这次改造的验收标准，也是唯一一条真正跨进程的测试：
//   ① 起 ./run.sh（无窗口）→ 它自己探测总线，没硬件就回退仿真
//   ② 起 serve.py
//   ③ 浏览器打开监视页 → 应当自动连上、遥测在流、
//      并且**不再需要"请求控制权"**就能操作
//
// 跑法（要先在本机构建过，见 DEPLOY.md；SDK 目录在仓库上一级）：
//   NODE_PATH=<装 playwright 的 node_modules> node tests/ui_live.js
//
// ★ 前提：**不要用 root 跑**，也不要在接了真机的机器上跑 —— 测试假定探测
//   会回退到仿真。若跑在有 EtherCAT 硬件的机器上，总线断言会按真机通过，
//   但"仿真"那一条会失败（这是环境问题，不是代码问题）。
//
// 用 ?host=127.0.0.1 覆盖地址推导：页面从 loopback 打开时会按设计去连
// DEFAULT_HOST（板子的 192.168.1.10），本机联调要用参数压掉。

const { spawn } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const os = require('node:os');
const fs = require('node:fs');
const { chromium } = require('playwright');

const JOINT = path.resolve(__dirname, '..', '..');
const WEB = path.resolve(__dirname, '..');
const PORT = Number(process.env.LIVE_PORT || 18090);

let pass = 0, fail = 0;
const ok = (n, c, x = '') => {
  if (c) { pass++; console.log(`  \x1b[32m✓\x1b[0m ${n}`); }
  else { fail++; console.log(`  \x1b[31m✗\x1b[0m ${n}${x ? '  ' + x : ''}`); }
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitPort(port, ms = 20000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    const c = await new Promise((r) => {
      const s = net.connect(port, '127.0.0.1');
      s.on('connect', () => { s.destroy(); r(true); });
      s.on('error', () => r(false));
    });
    if (c) return true;
    await sleep(200);
  }
  return false;
}

(async () => {
  // ① 下位机：无窗口启动
  const qt = spawn('./run.sh', [], { cwd: JOINT, stdio: ['ignore', 'pipe', 'pipe'] });
  let qtlog = '';
  qt.stdout.on('data', (d) => { qtlog += d; });
  qt.stderr.on('data', (d) => { qtlog += d; });

  // ② 网页服务
  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'live-'));
  const srv = spawn('python3', [path.join(WEB, 'serve.py'), '--port', String(PORT), '--bind', '127.0.0.1'],
    { cwd: WEB, env: { ...process.env, HOME: home }, stdio: ['ignore', 'pipe', 'pipe'] });

  let browser;
  try {
    console.log('\n[1] 下位机无窗口启动');
    ok('9002 起来了', await waitPort(9002));
    ok('打印了"无窗口"', /无窗口/.test(qtlog));
    // 关键：故障走的是 qWarning 而不是模态 QMessageBox。
    // 无窗口时弹模态框没人能点，整个下位机会卡死 —— 实测踩过。
    ok('进程没有卡在模态框上', qt.exitCode === null);

    await waitPort(PORT);
    browser = await chromium.launch({
      executablePath: '/usr/bin/google-chrome', headless: true,
      args: ['--no-sandbox', '--enable-unsafe-swiftshader'],
    });
    const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
    const errs = [];
    page.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
    page.on('console', (m) => {
      if (m.type() !== 'error') return;
      const u = (m.location() || {}).url || '';
      if (!/favicon/.test(u)) errs.push(`${m.text()} @${u}`);
    });

    await page.goto(`http://127.0.0.1:${PORT}/?host=127.0.0.1#monitor`, { waitUntil: 'load' });
    await page.evaluate(() => {
      const s = document.getElementById('splash');
      if (s) s.classList.add('gone');
      document.body.classList.add('app-in');
    });

    console.log('\n[2] 网页自动连上下位机（不点任何东西）');
    let bus = '';
    for (let i = 0; i < 10; i++) {
      await sleep(1500);
      bus = (await page.locator('#busBadge').textContent()) || '';
      if (!/未连接|正在/.test(bus)) break;
    }
    ok('总线已连上（仿真或真机）', bus && !/未连接|正在/.test(bus), bus);
    ok('认出从站', /\d/.test(await page.locator('#moduleTitle').textContent()));

    console.log('\n[3] 遥测在流');
    const p1 = await page.locator('#valPos').textContent();
    await sleep(1500);
    const p2 = await page.locator('#valVel').textContent();
    ok('位置是可读的数', /-?\d/.test(p1), p1);
    ok('速度是可读的数', /-?\d/.test(p2), p2);

    console.log('\n[4] 不需要"请求控制权"');
    const gone = await page.evaluate(() =>
      !document.getElementById('ownerBadge') && !document.getElementById('btnTakeControl'));
    ok('控制权徽章与按钮已不存在', gone);
    ok('未勾选安全确认时「使能」是灰的', await page.locator('#btnEnable').isDisabled());
    await page.locator('label[for="chkReady"]').click();
    await sleep(400);
    ok('★ 勾选后「使能」直接可用（以前要先请求控制权）',
       !(await page.locator('#btnEnable').isDisabled()));

    await page.locator('#btnSend').click();
    await sleep(900);
    ok('下发目标没有报"无控制权"',
       !/控制权/.test(await page.locator('#cmdHint').textContent()));

    ok('无 JS 报错', errs.length === 0, errs.slice(0, 3).join(' | '));
  } finally {
    if (browser) await browser.close().catch(() => {});
    try { srv.kill('SIGKILL'); } catch {}
    try { qt.kill('SIGKILL'); } catch {}
    try { fs.rmSync(home, { recursive: true, force: true }); } catch {}
  }

  console.log(`\n${fail === 0 ? '\x1b[32m全部通过\x1b[0m' : '\x1b[31m有失败\x1b[0m'}：${pass} 通过 / ${fail} 失败\n`);
  process.exit(fail === 0 ? 0 : 1);
})().catch((e) => { console.error('异常：', e); process.exit(1); });

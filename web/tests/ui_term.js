#!/usr/bin/env node
// 终端页的无头验证（playwright + 系统 Chrome）。
//
// 验的是**设计文档 §6.2 那四条**：
//   1. 切分 → 窗格数 +1
//   2. 拖分隔条 → 两边宽度真的变了
//   3. 切页往返 → shell 没被重建（echo $$ 前后一致），也没被 fit 成 0 列
//   4. 只剩一格时，关闭项是禁用的
//
// 跑法（NODE_PATH 指到装 playwright 的那个 node_modules）：
//   NODE_PATH=/home/k/.hermes/hermes-agent/node_modules node tests/ui_term.js
//
// 它会自己起 serve.py（临时端口），跑完自己收掉。

const { spawn } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
const { chromium } = require('playwright');

const PORT = Number(process.env.TERM_UI_PORT || 18091);
const WEB = path.resolve(__dirname, '..');
const BASE = `http://127.0.0.1:${PORT}/`;

let pass = 0, fail = 0;
const ok = (name, cond, extra = '') => {
  if (cond) { pass++; console.log(`  \x1b[32m✓\x1b[0m ${name}`); }
  else { fail++; console.log(`  \x1b[31m✗\x1b[0m ${name}${extra ? '  ' + extra : ''}`); }
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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

/** 读第 i 个窗格屏幕上渲染出来的文字（xterm 默认是 DOM 渲染器，行就在 DOM 里）。 */
async function screenText(page, i) {
  return page.evaluate((idx) => {
    const leaves = document.querySelectorAll('.term-leaf');
    const el = leaves[idx];
    if (!el) return '';
    return (el.querySelector('.xterm-rows') || el).innerText || '';
  }, i);
}

/** 一直等到屏幕上出现某个正则。 */
async function waitText(page, i, re, timeoutMs = 12000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const t = await screenText(page, i);
    const m = t.match(re);
    if (m) return m;
    await sleep(150);
  }
  throw new Error(`超时：窗格 ${i} 上等不到 ${re}`);
}

/** 屏幕上出现过的所有 LIVESHELL_<pid>。用"取最后一个"来判断 shell 有没有换人 ——
 *  终端是滚动的，两次结果本来就不会挨在一起。 */
async function allPids(page, i) {
  return (await screenText(page, i)).match(/LIVESHELL_(\d+)/g) || [];
}

/** 等到 LIVESHELL_ 的个数达到 n，返回最后一个 pid。 */
async function waitPid(page, i, n, timeoutMs = 12000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const hits = await allPids(page, i);
    if (hits.length >= n) return hits[hits.length - 1].replace('LIVESHELL_', '');
    await sleep(150);
  }
  throw new Error(`超时：窗格 ${i} 上只看到 ${(await allPids(page, i)).length} 次 LIVESHELL_，期望 ${n} 次`);
}

async function main() {
  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'termui-'));
  const srv = spawn('python3', [path.join(WEB, 'serve.py'), '--port', String(PORT), '--bind', '127.0.0.1'], {
    cwd: WEB,
    env: { ...process.env, HOME: home, SHELL: '/bin/bash' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let srvOut = '';
  srv.stdout.on('data', (d) => { srvOut += d; });
  srv.stderr.on('data', (d) => { srvOut += d; });

  let browser;
  try {
    if (!await waitPort(PORT)) throw new Error('serve.py 没起来：\n' + srvOut);

    browser = await chromium.launch({
      executablePath: '/usr/bin/google-chrome',
      headless: true,
      args: ['--no-sandbox', '--enable-unsafe-swiftshader', '--force-prefers-reduced-motion'],
    });
    const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
    // ★ 按**报错位置**过滤，不按文本：
    //   · /favicon.ico 是浏览器自己来要的，页面里根本没有这个文件（404 无害）
    //   · :9002 是无头环境连不上下位机，本测试不关心
    const errors = [];
    page.on('console', (m) => {
      if (m.type() !== 'error') return;
      const loc = (m.location() && m.location().url) || '';
      if (/favicon|:9002/.test(loc)) return;
      errors.push(`${m.text()}  @${loc}`);
    });
    page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));

    // 直接落到终端页（hash 路由），再把开场画面摘掉 —— 无头环境连不上下位机，
    // 正常流程会卡在开场画面上。
    await page.goto(BASE + '#terminal', { waitUntil: 'load' });
    await page.evaluate(() => {
      const s = document.getElementById('splash');
      if (s) s.classList.add('gone');
      document.body.classList.add('app-in');
    });
    await sleep(600);

    // ---- 0. 基本：一格 + 真的连上了 shell ---------------------------
    console.log('\n[0] 终端页起来了');
    let leaves = await page.locator('.term-leaf').count();
    ok('初始一格', leaves === 1, `实为 ${leaves}`);
    await page.locator('.term-leaf').first().click();
    await page.keyboard.type('echo LIVESHELL_$$\n');
    const pid1 = await waitPid(page, 0, 1);
    ok('shell 有回显（说明 PTY 通了）', !!pid1, `pid=${pid1}`);
    let d = await page.evaluate(() => Term.dump());
    ok('不是 0 列', d[0].cols > 20 && d[0].rows > 5, `cols=${d[0].cols} rows=${d[0].rows}`);

    // ---- 1. 切分 ----------------------------------------------------
    console.log('\n[1] 切分');
    await page.locator('.term-bar button[data-act="split-row"]').click();
    await sleep(500);
    leaves = await page.locator('.term-leaf').count();
    ok('左右切分 → 窗格 +1', leaves === 2, `实为 ${leaves}`);
    const dir = await page.locator('.term-split').first().getAttribute('class');
    ok('切分方向是 row', /term-split row/.test(dir || ''), dir || '');

    await page.locator('.term-bar button[data-act="split-col"]').click();
    await sleep(500);
    leaves = await page.locator('.term-leaf').count();
    ok('再上下切分 → 窗格 +1（嵌套）', leaves === 3, `实为 ${leaves}`);

    // ---- 2. 拖分隔条 ------------------------------------------------
    console.log('\n[2] 拖分隔条');
    const rootSplit = page.locator('.term-stage > .term-split').first();
    const before = await page.evaluate(() => {
      const s = document.querySelector('.term-stage > .term-split');
      return { a: s.children[0].style.width, b: s.children[2].style.width };
    });
    const bar = page.locator('.term-stage > .term-split > .term-div').first();
    const bb = await bar.boundingBox();
    await page.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2);
    await page.mouse.down();
    await page.mouse.move(bb.x + bb.width / 2 + 220, bb.y + bb.height / 2, { steps: 8 });
    await page.mouse.up();
    await sleep(400);
    const after = await page.evaluate(() => {
      const s = document.querySelector('.term-stage > .term-split');
      return { a: s.children[0].style.width, b: s.children[2].style.width };
    });
    ok('左边宽度变了', before.a !== after.a, `${before.a} → ${after.a}`);
    ok('右边宽度变了', before.b !== after.b, `${before.b} → ${after.b}`);

    // ---- 3. 切页往返，shell 要活着 ----------------------------------
    // 用 waitForFunction 等条件成立，不靠固定 sleep —— 切页过渡里新旧两页
    // 会**同时在场**，固定 sleep 有时会在旧页还没隐藏时就去读，读到一个假阴性。
    console.log('\n[3] 切到监控页再切回来');
    await page.evaluate(() => { location.hash = 'monitor'; });
    await page.waitForFunction(() => {
      const t = document.getElementById('page-terminal');
      return typeof Router !== 'undefined' && Router.current === 'monitor'
        && (t.hidden || getComputedStyle(t).display === 'none');
    }, null, { timeout: 6000 }).catch(async (e) => {
      const st = await page.evaluate(() => {
        const t = document.getElementById('page-terminal');
        return { cur: Router.current, hidden: t.hidden, disp: getComputedStyle(t).display };
      });
      throw new Error('没等来"离开终端页"：' + JSON.stringify(st));
    });
    ok('已经离开终端页', true);

    await page.evaluate(() => { location.hash = 'terminal'; });
    await page.waitForFunction(() => {
      const t = document.getElementById('page-terminal');
      return Router.current === 'terminal' && !t.hidden
        && getComputedStyle(t).display !== 'none';
    }, null, { timeout: 6000 });
    await sleep(600);          // 过渡走完再动手，避免点在被平移的页面上

    await page.locator('.term-leaf').first().click();
    await page.keyboard.type('echo LIVESHELL_$$\n');
    const pid2 = await waitPid(page, 0, 2);       // 第 2 次出现 → 这次的 PID
    ok('shell 还是同一个（PID 一致）', pid1 === pid2, `${pid1} vs ${pid2}`);

    d = await page.evaluate(() => Term.dump());
    const bad = d.filter((n) => n.kind === 'leaf' && (n.cols <= 20 || n.rows <= 5));
    ok('切回来没有哪一格被 fit 成 0 列', bad.length === 0,
       bad.map((b) => `#${b.id} ${b.cols}x${b.rows}`).join(' '));

    // ---- 4. 最后一格不给关 -------------------------------------------
    console.log('\n[4] 只剩一格时的关闭项');
    while (await page.locator('.term-leaf').count() > 1) {
      await page.locator('.term-leaf').last().click();
      await page.locator('.term-bar button[data-act="close"]').click();
      await sleep(400);
    }
    leaves = await page.locator('.term-leaf').count();
    ok('确实只剩一格', leaves === 1, `实为 ${leaves}`);

    await page.locator('.term-leaf').first().click({ button: 'right' });
    await sleep(300);
    const closeDisabled = await page.locator('.term-menu button', { hasText: '关闭窗格' }).isDisabled();
    ok('右键菜单里的「关闭窗格」是禁用的', closeDisabled);
    await page.keyboard.press('Escape');
    await page.mouse.click(30, 300);
    await sleep(200);

    // 工具栏那个按钮也应无效（只剩一格）
    await page.locator('.term-bar button[data-act="close"]').click();
    await sleep(300);
    ok('点关闭也关不掉（最后一格保底）', await page.locator('.term-leaf').count() === 1);

    // ---- 5. 最大化 ---------------------------------------------------
    console.log('\n[5] 最大化 / 还原');
    await page.locator('.term-bar button[data-act="split-row"]').click();
    await sleep(400);
    await page.locator('.term-leaf').first().click();
    await page.locator('.term-bar button[data-act="max"]').click();
    await sleep(400);
    const hasMax = await page.evaluate(() =>
      document.querySelector('.term-stage').classList.contains('maximized'));
    ok('最大化打上了标记', hasMax);
    const maxW = await page.evaluate(() => {
      const s = document.querySelector('.term-stage');
      return Math.round(s.querySelector('.term-leaf').getBoundingClientRect().width);
    });
    const stageW = await page.evaluate(() =>
      Math.round(document.querySelector('.term-stage').getBoundingClientRect().width));
    ok('最大化那格吃满了舞台宽', Math.abs(maxW - stageW) < 8, `${maxW} vs ${stageW}`);
    await page.locator('.term-bar button[data-act="max"]').click();
    await sleep(400);
    ok('再点一次还原', !(await page.evaluate(() =>
      document.querySelector('.term-stage').classList.contains('maximized'))));

    // ---- 收尾 --------------------------------------------------------
    await page.screenshot({ path: path.join(WEB, 'tests', 'term-preview.png') });
    console.log('\n截图 → web/tests/term-preview.png');

    const real = errors.filter((e) => !/favicon|9002|WebSocket/i.test(e));
    ok('控制台没有意外报错', real.length === 0, real.slice(0, 3).join(' | '));

  } finally {
    if (browser) await browser.close().catch(() => {});
    try { srv.kill('SIGKILL'); } catch {}
    try { fs.rmSync(home, { recursive: true, force: true }); } catch {}
  }

  console.log(`\n${fail === 0 ? '\x1b[32m全部通过\x1b[0m' : '\x1b[31m有失败\x1b[0m'}：${pass} 通过 / ${fail} 失败\n`);
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((e) => { console.error('\x1b[31m测试异常：\x1b[0m', e); process.exit(1); });

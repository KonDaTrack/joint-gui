/* 终端页 —— Terminator 式切分。
 *
 * 布局是一棵**二叉树**：节点要么是叶子（一片终端），要么是
 * 「横向/纵向切分 + 两个子节点 + 一个比例」。为什么不用 grid：
 * Terminator 的手感是"在任意一个已有的格子上再切一刀"，切完两边还可以不等分，
 * grid 表达不了这种嵌套不等分，二叉树天然就是它。
 *
 * 三条不能破的约定（同 router.js / chart.js）：
 *   1) 切页**不重建 DOM** —— 离开终端页 shell 继续跑，回来接着用
 *   2) 页面 hidden 时所有元素尺寸为 0，**绝不能**在这时候 fit
 *      （FitAddon 会算出 0 列 0 行，把终端尺寸写坏）—— 和 chart.js 踩过的坑同源
 *   3) 相对后端的协议：**二进制帧 = 终端数据，文本帧 = 控制消息**。
 *      按键走二进制，否则用户在命令行里敲出 {"t":"resize",...} 会被当成控制消息吞掉
 *
 * 终端地址是**同源**的（ws://${location.host}/term）：静态文件和终端同端口，
 * 所以不用写死任何主机名和端口，页面搬到哪台机器都自动跟着走。
 */

const Term = (() => {
  'use strict';

  const GAP = 6;          // 分隔条粗细，和 terminal.css 里一致
  const MIN_W = 90;       // 拖动时每格的最小宽/高（px）
  const MIN_H = 70;
  const ENC = new TextEncoder();

  // 配色照抄 app.css 的色板 —— 终端页和别的页是一套东西，不该看起来像贴上去的
  const THEME = {
    background: '#0E0F11',        // --sunken
    foreground: '#E7E9EC',        // --fg
    cursor: '#78D6EE',            // --cyan
    cursorAccent: '#0E0F11',
    selectionBackground: 'rgba(120, 214, 238, 0.25)',
    black: '#1B1C1E', red: '#E5484D', green: '#4ECB8E', yellow: '#D9A441',
    blue: '#5AB0F0', magenta: '#B98CF0', cyan: '#78D6EE', white: '#C7CDD4',
    brightBlack: '#666C75', brightRed: '#EF7A85', brightGreen: '#7FD1B9',
    brightYellow: '#E8A25C', brightBlue: '#78D6EE', brightMagenta: '#C9A9F5',
    brightCyan: '#C6F2FF', brightWhite: '#F2F5F8',
  };

  let inited = false;
  let hostEl = null;        // #termRoot
  let stage = null;         // .term-stage
  let tree = null;          // 根节点
  let active = null;        // 当前窗格（切分/关闭都作用于它）
  let maxLeaf = null;
  let menuEl = null;
  let seq = 0;

  const leaves = [];        // 只为了 fitAll / 计数，树本身才是真相

  // ---------------------------------------------------------------- 工具

  const box = (el, o) => { for (const k in o) el.style[k] = o[k]; };

  function current() {
    return typeof Router !== 'undefined' && Router.current === 'terminal';
  }

  // ---------------------------------------------------------------- 叶子

  function makeLeaf() {
    const id = ++seq;
    const el = document.createElement('div');
    el.className = 'term-node term-leaf';
    el.dataset.leaf = String(id);

    const mount = document.createElement('div');
    mount.className = 'term-mount';
    el.appendChild(mount);

    const ov = document.createElement('div');
    ov.className = 'term-overlay';
    ov.innerHTML =
      '<div class="msg"></div><div class="why"></div>' +
      '<button class="btn btn-primary" type="button">重连</button>';
    el.appendChild(ov);

    const leaf = {
      kind: 'leaf', id, el, mount, ov,
      term: null, fit: null, ws: null, ro: null,
      parent: null, exited: false, fatal: false,
    };

    ov.querySelector('button').addEventListener('click', () => restart(leaf));
    el.addEventListener('pointerdown', () => setActive(leaf), true);
    el.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      setActive(leaf);
      openMenu(e.clientX, e.clientY, leaf);
    });

    leaves.push(leaf);      // 登记在这里而不是调用方：切分和初次建树都走这一条路
    return leaf;
  }

  function startShell(leaf) {
    leaf.exited = false;
    leaf.fatal = false;
    leaf.el.classList.remove('dead');

    if (!window.Terminal || !window.FitAddon) {
      showOverlay(leaf, '终端组件没加载', 'lib/xterm/ 下的文件没取到');
      leaf.fatal = true;
      return;
    }

    if (!leaf.term) {
      const term = new window.Terminal({
        fontFamily: "'JetBrains Mono','DejaVu Sans Mono',Consolas,monospace",
        fontSize: 13,
        lineHeight: 1.15,
        letterSpacing: 0,
        cursorBlink: true,
        cursorStyle: 'bar',
        scrollback: 5000,
        allowProposedApi: true,
        theme: THEME,
      });
      const fit = new window.FitAddon.FitAddon();
      term.loadAddon(fit);
      term.open(leaf.mount);

      // ★ 按键走**二进制帧**（见文件头第 3 条）
      term.onData((d) => {
        const ws = leaf.ws;
        if (ws && ws.readyState === WebSocket.OPEN) ws.send(ENC.encode(d));
      });
      // xterm 对"高位字节"的输入单独走 onBinary（onData 只给 UTF-16 字符串）
      term.onBinary((d) => {
        const ws = leaf.ws;
        if (!ws || ws.readyState !== WebSocket.OPEN) return;
        const b = new Uint8Array(d.length);
        for (let i = 0; i < d.length; i++) b[i] = d.charCodeAt(i) & 0xff;
        ws.send(b);
      });
      term.onResize(() => sendResize(leaf));

      leaf.term = term;
      leaf.fit = fit;
      leaf.ro = new ResizeObserver(() => fitLeaf(leaf));
      leaf.ro.observe(leaf.el);       // 观察叶子本身（无 padding），0 尺寸判断才准
    }

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    let ws;
    try {
      ws = new WebSocket(`${proto}//${location.host}/term`);
    } catch (e) {
      showOverlay(leaf, '打不开终端连接', e.message || '');
      return;
    }
    ws.binaryType = 'arraybuffer';
    leaf.ws = ws;

    ws.onopen = () => {
      leaf.el.classList.remove('dead');
      fitLeaf(leaf);
      try { leaf.term.focus(); } catch (e) {}
    };
    ws.onmessage = (ev) => {
      if (typeof ev.data === 'string') { control(leaf, ev.data); return; }
      try { leaf.term.write(new Uint8Array(ev.data)); } catch (e) {}
    };
    ws.onclose = () => {
      if (leaf.ws !== ws) return;     // 已被重连顶替，忽略这一次
      if (leaf.fatal || leaf.el.classList.contains('dead')) return;
      if (leaf.exited) showOverlay(leaf, '这个 shell 已退出', '按「重连」开一个新 shell');
      else showOverlay(leaf, '连接已断开', '按「重连」开一个新 shell');
    };
    ws.onerror = () => {};
  }

  function control(leaf, text) {
    let msg;
    try { msg = JSON.parse(text); } catch (e) { return; }
    if (msg.t === 'exit') { leaf.exited = true; return; }
    if (msg.t === 'error') {
      leaf.fatal = true;
      showOverlay(leaf, '终端不可用', msg.message || '');
    }
  }

  function showOverlay(leaf, msg, why) {
    leaf.el.classList.add('dead');
    leaf.ov.querySelector('.msg').textContent = msg;
    leaf.ov.querySelector('.why').textContent = why || '';
    // 平台不支持这种是没救的，别给一个点了也没用的按钮
    leaf.ov.querySelector('button').hidden = !!leaf.fatal;
  }

  function restart(leaf) {
    const old = leaf.ws;
    leaf.ws = null;
    if (old) { old.onclose = null; old.onmessage = null; try { old.close(); } catch (e) {} }
    if (leaf.term) { try { leaf.term.reset(); } catch (e) {} }
    startShell(leaf);
  }

  function disposeLeaf(leaf) {
    const i = leaves.indexOf(leaf);
    if (i >= 0) leaves.splice(i, 1);
    try { if (leaf.ro) leaf.ro.disconnect(); } catch (e) {}
    if (leaf.ws) { leaf.ws.onclose = null; leaf.ws.onmessage = null; try { leaf.ws.close(); } catch (e) {} }
    try { if (leaf.term) leaf.term.dispose(); } catch (e) {}
    leaf.el.remove();
    if (active === leaf) active = null;
    if (maxLeaf === leaf) maxLeaf = null;
  }

  // ---------------------------------------------------------------- 尺寸

  function fitLeaf(leaf) {
    if (!leaf.fit || !leaf.el.isConnected) return;
    // ★ 页面 hidden 时尺寸为 0，这时 fit 会把终端写成 0 列 —— 直接跳过
    if (!leaf.el.clientWidth || !leaf.el.clientHeight) return;
    try { leaf.fit.fit(); } catch (e) {}
  }

  function fitAll() {
    for (const l of leaves.slice()) fitLeaf(l);
  }

  function sendResize(leaf) {
    const ws = leaf.ws;
    if (!ws || ws.readyState !== WebSocket.OPEN || !leaf.term) return;
    ws.send(JSON.stringify({ t: 'resize', cols: leaf.term.cols, rows: leaf.term.rows }));
  }

  // ---------------------------------------------------------------- 布局

  function applyRatio(s) {
    const p = (s.ratio * 100).toFixed(4);
    const q = ((1 - s.ratio) * 100).toFixed(4);
    const h = GAP / 2;
    if (s.dir === 'row') {
      box(s.a.el, { left: '0', top: '0', width: `calc(${p}% - ${h}px)`, height: '100%' });
      box(s.div, { left: `calc(${p}% - ${h}px)`, top: '0', width: `${GAP}px`, height: '100%' });
      box(s.b.el, { left: `calc(${p}% + ${h}px)`, top: '0', width: `calc(${q}% - ${h}px)`, height: '100%' });
    } else {
      box(s.a.el, { left: '0', top: '0', width: '100%', height: `calc(${p}% - ${h}px)` });
      box(s.div, { left: '0', top: `calc(${p}% - ${h}px)`, width: '100%', height: `${GAP}px` });
      box(s.b.el, { left: '0', top: `calc(${p}% + ${h}px)`, width: '100%', height: `calc(${q}% - ${h}px)` });
    }
  }

  function wireDivider(s) {
    s.div.addEventListener('pointerdown', (e) => {
      if (maxLeaf) return;
      e.preventDefault();
      e.stopPropagation();
      s.div.setPointerCapture(e.pointerId);
      s.div.classList.add('dragging');

      const move = (ev) => {
        const r = s.el.getBoundingClientRect();
        const total = s.dir === 'row' ? r.width : r.height;
        if (total < 1) return;
        let v = s.dir === 'row' ? (ev.clientX - r.left) / total
                                : (ev.clientY - r.top) / total;
        const minPx = s.dir === 'row' ? MIN_W : MIN_H;
        const minR = Math.min(0.45, minPx / total);   // 钳制：两边都不许被压没
        s.ratio = Math.max(minR, Math.min(1 - minR, v));
        applyRatio(s);
        fitLeaf(s.a); fitLeaf(s.b);      // 只重测这两格，别整树 fit
      };
      const up = () => {
        s.div.removeEventListener('pointermove', move);
        s.div.classList.remove('dragging');
        try { s.div.releasePointerCapture(e.pointerId); } catch (err) {}
        fitAll();
      };
      s.div.addEventListener('pointermove', move);
      s.div.addEventListener('pointerup', up, { once: true });
      s.div.addEventListener('pointercancel', up, { once: true });
    });
  }

  // ---------------------------------------------------------------- 操作

  function split(leaf, dir) {
    if (!leaf) return;
    clearMax();                       // 最大化状态下先还原，免得树和 DOM 打架

    const sib = makeLeaf();
    const s = { kind: 'split', dir, ratio: 0.5, a: leaf, b: sib, parent: leaf.parent, el: null, div: null };

    const el = document.createElement('div');
    el.className = `term-node term-split ${dir}`;
    const div = document.createElement('div');
    div.className = 'term-div';

    const gp = leaf.parent;
    if (gp) { if (gp.a === leaf) gp.a = s; else gp.b = s; }
    else tree = s;

    leaf.el.replaceWith(el);
    el.appendChild(leaf.el);
    el.appendChild(div);
    el.appendChild(sib.el);

    leaf.parent = s;
    sib.parent = s;
    s.el = el;
    s.div = div;
    wireDivider(s);
    applyRatio(s);

    startShell(sib);
    setActive(sib);
    fitAll();
    return sib;
  }

  function closeLeaf(leaf) {
    // 至少留一格：只剩一格时关闭会把整页变空，而且没有回来的路
    if (!leaf || leaf === tree) return false;
    clearMax();                       // 最大化状态下关窗格，先把标记收干净
    const par = leaf.parent;
    if (!par) return false;

    const sib = par.a === leaf ? par.b : par.a;
    const gp = par.parent;
    sib.parent = gp;
    if (gp) { if (gp.a === par) gp.a = sib; else gp.b = sib; }
    else tree = sib;

    sib.el.remove();
    par.el.replaceWith(sib.el);

    if (gp) {
      applyRatio(gp);
    } else {
      // 顶上来当了根：清掉 inline 定位，让 `.term-stage > .term-node` 那条规则接管
      box(sib.el, { left: '', top: '', width: '', height: '' });
    }

    disposeLeaf(leaf);
    setActive(sib);
    fitAll();
    return true;
  }

  function toggleMax(leaf) {
    if (!leaf) return;
    if (maxLeaf === leaf) { clearMax(); return; }
    clearMax();
    maxLeaf = leaf;
    stage.classList.add('maximized');

    // 沿"根 → 目标叶子"这一条路打标记：路上每个 split 标 .max-path，
    // 它的**旁支兄弟**标 .max-hidden。旁支 display:none 之后，
    // 路上那个节点靠 CSS 的 !important 吃满，不用重算任何尺寸。
    let child = leaf;
    while (child.parent) {
      const s = child.parent;
      s.el.classList.add('max-path');
      (s.a === child ? s.b : s.a).el.classList.add('max-hidden');
      child = s;
    }
    fitAll();
  }

  function clearMax() {
    if (!maxLeaf) return;
    stage.classList.remove('maximized');
    stage.querySelectorAll('.max-path').forEach((e) => e.classList.remove('max-path'));
    stage.querySelectorAll('.max-hidden').forEach((e) => e.classList.remove('max-hidden'));
    maxLeaf = null;
    fitAll();
  }

  function setActive(leaf) {
    if (active === leaf) { if (leaf && leaf.term) { try { leaf.term.focus(); } catch (e) {} } return; }
    if (active) active.el.classList.remove('active');
    active = leaf;
    if (leaf) {
      leaf.el.classList.add('active');
      if (leaf.term) { try { leaf.term.focus(); } catch (e) {} }
    }
  }

  function doAction(act, leaf) {
    const target = leaf || active || tree;
    if (!target) return;
    if (act === 'split-row') split(target, 'row');
    else if (act === 'split-col') split(target, 'col');
    else if (act === 'close') closeLeaf(target);
    else if (act === 'max') toggleMax(target);
  }

  // ---------------------------------------------------------------- 菜单

  function closeMenu() {
    if (menuEl) { menuEl.remove(); menuEl = null; }
  }

  function openMenu(x, y, leaf) {
    closeMenu();
    const m = document.createElement('div');
    m.className = 'term-menu';

    const items = [
      { act: 'split-row', label: '左右切分', key: '⌃⇧O' },
      { act: 'split-col', label: '上下切分', key: '⌃⇧E' },
      { sep: true },
      { act: 'max', label: maxLeaf === leaf ? '还原' : '最大化', key: '⌃⇧X' },
      { act: 'close', label: '关闭窗格', key: '⌃⇧W', disabled: leaf === tree },
    ];

    for (const it of items) {
      if (it.sep) {
        const s = document.createElement('div');
        s.className = 'sep';
        m.appendChild(s);
        continue;
      }
      const b = document.createElement('button');
      b.type = 'button';
      b.innerHTML = `<span>${it.label}</span><span class="k">${it.key}</span>`;
      if (it.disabled) b.disabled = true;
      else b.addEventListener('click', () => { closeMenu(); doAction(it.act, leaf); });
      m.appendChild(b);
    }

    m.style.left = Math.max(4, Math.min(x, window.innerWidth - 190)) + 'px';
    m.style.top = Math.max(4, Math.min(y, window.innerHeight - 210)) + 'px';
    document.body.appendChild(m);
    menuEl = m;
  }

  // ---------------------------------------------------------------- 快捷键

  // ★ 用**捕获阶段**挂在 document 上：这样在 xterm 自己的 keydown 处理之前就
  //   stopPropagation 掉，按键根本到不了 shell —— 不需要 attachCustomKeyEventHandler。
  //   （Ctrl+Shift+E 在 bash 里有自己的含义，不拦就会被吃掉。）
  function onKey(e) {
    if (!e.ctrlKey || !e.shiftKey || e.altKey || e.metaKey) return;
    let act = null;
    switch (e.code) {
      case 'KeyO': act = 'split-row'; break;
      case 'KeyE': act = 'split-col'; break;
      case 'KeyW': act = 'close'; break;
      case 'KeyX': act = 'max'; break;
      default: return;
    }
    if (!current()) return;
    e.preventDefault();
    e.stopPropagation();
    doAction(act, active || tree);
  }

  // ---------------------------------------------------------------- 生命周期

  function ensure() {
    if (tree) { fitAll(); return; }
    if (!hostEl) return;

    stage = document.createElement('div');
    stage.className = 'term-stage';
    hostEl.appendChild(stage);

    tree = makeLeaf();
    stage.appendChild(tree.el);
    startShell(tree);
    setActive(tree);

    requestAnimationFrame(() => fitAll());
  }

  function init() {
    if (inited) return;
    inited = true;
    hostEl = document.getElementById('termRoot');
    if (!hostEl) return;

    // 懒初始化：**没进过终端页就不开 shell**。进了之后 shell 就一直活着，
    // 切走再切回来还是同一个（同监控页的波形、三维）。
    if (typeof Router !== 'undefined') {
      Router.onChange((page) => { if (page === 'terminal') ensure(); });
    }

    document.addEventListener('keydown', onKey, true);
    document.addEventListener('pointerdown', (e) => {
      if (menuEl && !menuEl.contains(e.target)) closeMenu();
    }, true);
    window.addEventListener('blur', closeMenu);

    const bar = document.querySelector('.term-bar');
    if (bar) {
      bar.addEventListener('click', (e) => {
        const b = e.target.closest('button[data-act]');
        if (b) doAction(b.dataset.act, active || tree);
      });
    }
  }

  return {
    init,
    ensure,
    split,
    close: closeLeaf,
    maximize: toggleMax,
    fitAll,
    do: doAction,
    theme: THEME,
    get root() { return tree; },
    get active() { return active; },
    countLeaves() { return leaves.length; },
    /** 给测试用：返回每个切分节点的方向与比例 */
    dump() {
      const out = [];
      (function walk(n) {
        if (!n) return;
        if (n.kind === 'leaf') {
          out.push({
            kind: 'leaf', id: n.id,
            dead: n.el.classList.contains('dead'),
            cols: n.term ? n.term.cols : 0,
            rows: n.term ? n.term.rows : 0,
            w: Math.round(n.el.clientWidth),
            h: Math.round(n.el.clientHeight),
          });
          return;
        }
        out.push({ kind: 'split', dir: n.dir, ratio: n.ratio,
                   a: n.a.el.style.width || n.a.el.style.height,
                   b: n.b.el.style.width || n.b.el.style.height });
        walk(n.a); walk(n.b);
      })(tree);
      return out;
    },
  };
})();

Term.init();

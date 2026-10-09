// 历史对比图：把勾选的几次运行**叠在同一张图上**。
//
// 分色规则（最容易糊的一处）：
//   **运行 = 颜色**（8 色调色板）    **轨迹 = 线型**（位置实线 / 速度虚线 / 力矩点线）
//
// 为什么不"轨迹=颜色"：那是实时波形图的规则（位置青/速度绿/力矩琥珀）。
// 对比场景下主角是"运行之间的差异"，颜色必须留给运行。轨迹改用线型区分 ——
// 三条轨迹最多三种线型，够用且不冲突。
//
// 时间轴按 **t=0 对齐**，不各自缩放到满宽：缩放会把"响应慢"也拉成"响应快"，
// 教学上是错的（对比要看的就是谁快谁慢）。
//
// 每个窗格的 Y 量程取**所有已选运行的并集**，不是各自缩放 —— 各自缩放会让
// 两条本来差很多的曲线看起来一样。

const TRACE_META = [
  { key: 'positionDeg', label: '位置', unit: 'deg',   color: '#78D6EE', dash: [],      digits: 2 },
  { key: 'velocityDps', label: '速度', unit: 'deg/s', color: '#4ECB8E', dash: [7, 5],  digits: 1 },
  { key: 'torqueNm',    label: '力矩', unit: 'N·m',   color: '#D9A441', dash: [2, 4],  digits: 3 },
];

// 8 色调色板：饱和度压过，避免抢过界面的青色主色
const PALETTE = ['#78D6EE', '#4ECB8E', '#D9A441', '#B98CF0',
                 '#EF7A85', '#5AB0F0', '#E8A25C', '#7FD1B9'];

const Compare = (() => {
  let canvas, ctx, dpr = 1;
  let runs = [];            // [{ name, ts, traces, color, ... }]
  let traceKeys = TRACE_META.map((t) => t.key);
  let cursor = null;

  const PAD = { l: 10, r: 10, t: 8, b: 16 };
  const hexA = (hex, a) => {
    const n = parseInt(hex.slice(1), 16);
    return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`;
  };

  let mounted = false;
  function mount() {
    canvas = document.getElementById('cmpChart');
    if (!canvas) return;
    // 幂等：每次切到历史页都会调一次，重复挂会叠出重复的监听器
    if (mounted) { draw(); return; }
    mounted = true;
    ctx = canvas.getContext('2d');
    const resize = () => {
      const r = canvas.getBoundingClientRect();
      // 尺寸为 0 时不写画布（页面隐藏时 ResizeObserver 会带 0 触发）——
      // 同 chart.js 的坑：照写会清零，而重新显示时它不一定再触发
      if (!r.width || !r.height) return;
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = r.width * dpr;
      canvas.height = r.height * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      draw();
    };
    new ResizeObserver(resize).observe(canvas);
    canvas.addEventListener('mousemove', (e) => {
      cursor = e.clientX - canvas.getBoundingClientRect().left;
      draw();
    });
    canvas.addEventListener('mouseleave', () => { cursor = null; draw(); });
    resize();
  }

  /** @param {Array} list 选中的运行（按选择顺序），颜色按顺序分配 */
  function setRuns(list) {
    runs = list.map((r, i) => Object.assign({}, r, { color: PALETTE[i % PALETTE.length] }));
    runs.forEach((r, i) => { r.color = PALETTE[i % PALETTE.length]; });
    draw();
  }

  function setTraces(keys) { traceKeys = keys.slice(); draw(); }

  /** 给左侧列表用的颜色（按它在已选里的次序） */
  function colorFor(index) { return PALETTE[index % PALETTE.length]; }

  function draw() {
    if (!ctx) return;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w || !h) return;
    ctx.clearRect(0, 0, w, h);

    const metas = TRACE_META.filter((m) => traceKeys.includes(m.key));
    if (!metas.length || !runs.length) return;

    const n = metas.length;
    const gap = 14;
    const paneH = (h - PAD.t - PAD.b - gap * (n - 1)) / n;
    const xL = PAD.l, xR = w - PAD.r;

    // 时间轴按**最长的**那一次定标（t=0 对齐，不各自缩放）
    let maxN = 1;
    runs.forEach((r) => metas.forEach((m) => {
      const b = r.traces[m.key];
      if (b && b.length > maxN) maxN = b.length;
    }));

    metas.forEach((m, k) => {
      const top = PAD.t + k * (paneH + gap);

      // 量程取**所有已选运行的并集** —— 各自缩放会让差很多的曲线看起来一样
      let lo = Infinity, hi = -Infinity;
      runs.forEach((r) => {
        const b = r.traces[m.key];
        if (!b || !b.length) return;
        for (let i = 0; i < b.length; i++) {
          if (b[i] < lo) lo = b[i];
          if (b[i] > hi) hi = b[i];
        }
      });
      if (!Number.isFinite(lo)) { lo = 0; hi = 1; }
      let span = (hi - lo) * 1.12;
      if (span < 1e-9) span = 1;
      const c = (lo + hi) / 2;
      lo = c - span / 2;
      const yAt = (v) => top + paneH - ((v - lo) / span) * paneH;

      // 窗格底 + 中位线
      ctx.fillStyle = 'rgba(0,0,0,0.30)';
      ctx.fillRect(xL, top, xR - xL, paneH);
      ctx.strokeStyle = 'rgba(255,255,255,0.07)';
      ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(xL, top + 0.5); ctx.lineTo(xR, top + 0.5);
      ctx.moveTo(xL, top + paneH - 0.5); ctx.lineTo(xR, top + paneH - 0.5); ctx.stroke();
      ctx.strokeStyle = 'rgba(255,255,255,0.035)';
      ctx.setLineDash([3, 5]);
      ctx.beginPath(); ctx.moveTo(xL, top + paneH / 2); ctx.lineTo(xR, top + paneH / 2); ctx.stroke();
      ctx.setLineDash([]);
      // 时间竖网格
      ctx.strokeStyle = 'rgba(255,255,255,0.045)';
      for (let i = 1; i < 8; i++) {
        const x = xL + (xR - xL) * i / 8;
        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + paneH); ctx.stroke();
      }

      // 每条运行一条线：颜色＝运行，线型＝轨迹
      runs.forEach((r) => {
        const b = r.traces[m.key];
        if (!b || b.length < 2) return;
        ctx.beginPath();
        const xSpan = Math.max(1, b.length - 1);
        for (let i = 0; i < b.length; i++) {
          const x = xL + (i / xSpan) * (xR - xL);
          const y = yAt(b[i]);
          i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        }
        ctx.strokeStyle = r.color;
        ctx.lineWidth = 1.6;
        ctx.setLineDash(m.dash);
        ctx.shadowColor = r.color;
        ctx.shadowBlur = 5;
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.shadowBlur = 0;
      });

      // 窗格标题：名称 + 量程，左边画一段**线型示意**。
      // ★ 示意必须用中性色：图上的线是"按运行上色"的（运行=颜色），
      //   用轨迹色画个 ■ 会误导 —— 那块颜色在图上根本不存在。
      ctx.textAlign = 'left';
      ctx.font = '11px monospace';
      ctx.strokeStyle = '#9BA1A9';
      ctx.lineWidth = 1.8;
      ctx.setLineDash(m.dash);
      ctx.beginPath(); ctx.moveTo(xL + 4, top + 8); ctx.lineTo(xL + 26, top + 8); ctx.stroke();
      ctx.setLineDash([]);
      const f = (v) => (Math.abs(v) < 10 ? v.toFixed(2) : v.toFixed(1));
      ctx.fillText(`${m.label}  ${f(lo)}~${f(lo + span)}`, xL + 33, top + 12);
    });

    // 游标：竖线 + 每条运行在该时刻的点
    if (cursor != null && cursor > xL && cursor < xR) {
      const u = (cursor - xL) / (xR - xL);
      ctx.strokeStyle = 'rgba(255,255,255,0.35)';
      ctx.lineWidth = 1;
      ctx.setLineDash([2, 3]);
      ctx.beginPath(); ctx.moveTo(cursor, PAD.t); ctx.lineTo(cursor, h - PAD.b); ctx.stroke();
      ctx.setLineDash([]);

      metas.forEach((m, k) => {
        const top = PAD.t + k * (paneH + gap);
        let lo2 = Infinity, hi2 = -Infinity;
        runs.forEach((r) => {
          const b = r.traces[m.key];
          if (b && b.length) for (const v of b) { if (v < lo2) lo2 = v; if (v > hi2) hi2 = v; }
        });
        let span2 = (hi2 - lo2) * 1.12; if (!(span2 > 0)) span2 = 1;
        const lo3 = (lo2 + hi2) / 2 - span2 / 2;
        runs.forEach((r) => {
          const b = r.traces[m.key];
          if (!b || !b.length) return;
          const i = Math.round(u * (b.length - 1));
          const y = top + paneH - ((b[i] - lo3) / span2) * paneH;
          ctx.beginPath();
          ctx.arc(cursor, Math.max(top + 3, Math.min(y, top + paneH - 3)), 3.4, 0, Math.PI * 2);
          ctx.fillStyle = r.color; ctx.fill();
          ctx.strokeStyle = 'rgba(0,0,0,0.6)'; ctx.lineWidth = 1; ctx.stroke();
        });
      });
    }

    // 时间轴脚注
    ctx.textAlign = 'left';
    ctx.font = '11px monospace';
    ctx.fillStyle = '#666C75';
    ctx.fillText('t=0', xL, h - 3);
    ctx.textAlign = 'right';
    ctx.fillStyle = '#9BA1A9';
    ctx.fillText(`${runs.length} 次运行 · 实线=位置 虚线=速度 点线=力矩`, xR, h - 3);
  }

  return { mount, setRuns, setTraces, colorFor, redraw: draw };
})();

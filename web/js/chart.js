// 实时波形：Canvas 手绘，三条轨迹**分成三个窗格**纵排。
//
// 为什么分窗而不是叠在一起：三条轨迹各自独立自动缩放，叠放时峰值**永远落在
// 同一条水平线上**、互相盖住（绿线被橙线压掉是常态）。分窗之后各占一条带，
// 谁在动、动了多少一目了然，而且每个窗格可以标自己的量程。
//
// 与 Qt 端 CurvePanel 保持同样的取舍：
//  1) 平时不采样、不显示；点「下发目标」才开始记录本次响应
//  2) 每条轨迹独立自动缩放，但有**最小量程**——否则静止时的微小抖动会被拉伸到
//     满窗，看着像剧烈震荡（这个坑在 Qt 端踩过）
//  3) 量程平滑跟随，避免每帧重算导致波形整体跳动

// 与 css 的变量同源（canvas 读不到 CSS 变量，只能重复一遍）。
// 颜色与左边的径向仪表对齐：位置青 / 速度绿 / 力矩琥珀。
const TRACES = [
  { key: 'positionDeg', color: '#78D6EE', label: '位置', unit: 'deg',   minSpan: 1.0,  digits: 2 },
  { key: 'velocityDps', color: '#4ECB8E', label: '速度', unit: 'deg/s', minSpan: 10.0, digits: 1 },
  { key: 'torqueNm',    color: '#D9A441', label: '力矩', unit: 'N·m',   minSpan: 1.0,  digits: 3 },
];

const MAX_POINTS = 3000;   // 10s @300Hz 上限，够装下一次完整运动
const PAD = { l: 8, r: 8, t: 6, b: 14 };

const hexA = (hex, a) => {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`;
};

class Chart {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.recording = false;
    this.traces = TRACES.map((t) => ({ ...t, buf: [], center: 0, span: 0, scaled: false }));
    this.targets = {};        // trace.key → 目标值（下发目标时传入）
    this.moved = false;
    this.stillSince = 0;
    this.startTs = 0;
    this.lastTs = 0;
    this.cursor = null;       // 鼠标位置的 x（画布坐标），null = 不画游标

    // 高分屏清晰度
    const dpr = window.devicePixelRatio || 1;
    const resize = () => {
      const r = canvas.getBoundingClientRect();
      // ★ 尺寸为 0 时直接返回，**不要**写 canvas.width/height。
      //   页面被 display:none 隐藏时 ResizeObserver 会带着 0 触发一次，
      //   照写就把画布清零了；而页面重新显示时它**不一定**会再触发
      //   （实测三次里两次不触发），波形就一直是空白的，且从界面上看不出原因。
      if (!r.width || !r.height) return;
      canvas.width = r.width * dpr;
      canvas.height = r.height * dpr;
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      this.draw();
    };
    // 供外部在页面切换后**强制重测**。光靠 ResizeObserver 不够 —— 见上面的竞态。
    this.resize = resize;
    new ResizeObserver(resize).observe(canvas);
    resize();

    canvas.addEventListener('mousemove', (e) => {
      const r = canvas.getBoundingClientRect();
      this.cursor = e.clientX - r.left;
      this.draw();
    });
    canvas.addEventListener('mouseleave', () => { this.cursor = null; this.draw(); });
  }

  /**
   * 点「下发目标」时调用：清空并开始记录本次响应
   * @param {number} ratedTorqueNm 额定力矩（力矩最小量程按它取 10%）
   * @param {object} targets       { traceKey: 目标值 }，用于画目标虚线
   */
  start(ratedTorqueNm, targets) {
    this.traces.forEach((t) => {
      t.buf = []; t.scaled = false;
      // 力矩最小量程按额定取 10%：驱动电流估算的力矩本身有约 1% 额定的纹波，
      // 固定量程会让大关节被这点纹波占满整屏
      if (t.key === 'torqueNm' && ratedTorqueNm > 0) t.minSpan = ratedTorqueNm * 0.1;
    });
    this.targets = targets || {};
    this.recording = true;
    this.moved = false;
    this.stillSince = 0;
    this.startTs = 0;
    this.draw();
  }

  stop() { this.recording = false; this.draw(); }

  /** 每次收到所选从站的遥测调用一次 */
  push(t, nowMs) {
    if (!this.recording) return;
    if (!this.startTs) this.startTs = nowMs;
    this.lastTs = nowMs;

    this.traces.forEach((tr) => {
      tr.buf.push(t[tr.key]);
      if (tr.buf.length > MAX_POINTS) tr.buf.shift();
    });

    // 运动完成自动收尾：先"动过"，再连续静止 500ms
    const still = Math.abs(t.velocityDps) < 0.5;
    if (!still) { this.moved = true; this.stillSince = 0; }
    else if (this.moved) {
      if (!this.stillSince) this.stillSince = nowMs;
      else if (nowMs - this.stillSince > 500) this.recording = false;
    }
    if (this.traces[0].buf.length >= MAX_POINTS) this.recording = false;

    this.draw();
  }

  /** 每个窗格的量程：自动跟随但有下限，且平滑过渡 */
  _scale(tr) {
    const lo0 = Math.min(...tr.buf), hi0 = Math.max(...tr.buf);
    const targetSpan = Math.max(tr.minSpan, (hi0 - lo0) * 1.2);
    const targetCenter = (lo0 + hi0) / 2;
    if (!tr.scaled) { tr.center = targetCenter; tr.span = targetSpan; tr.scaled = true; }
    else { tr.center += (targetCenter - tr.center) * 0.15; tr.span += (targetSpan - tr.span) * 0.15; }
  }

  draw() {
    const ctx = this.ctx;
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    if (!w || !h) return;
    ctx.clearRect(0, 0, w, h);

    const hasData = this.traces.some((t) => t.buf.length > 1);
    if (!hasData) {
      ctx.fillStyle = '#666C75';
      ctx.font = '15px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('点「下发目标」后开始记录波形', w / 2, h / 2);
      return;
    }

    // 三个窗格纵排
    const n = this.traces.length;
    const usableH = h - PAD.t - PAD.b;
    const gap = 12;
    const paneH = (usableH - gap * (n - 1)) / n;
    const xL = PAD.l, xR = w - PAD.r;
    const nPts = this.traces[0].buf.length;
    const xAt = (i) => xL + (nPts < 2 ? 0 : i / (nPts - 1)) * (xR - xL);

    this.traces.forEach((tr, k) => {
      const top = PAD.t + k * (paneH + gap);
      this._scale(tr);
      const lo = tr.center - tr.span / 2;
      const yAt = (v) => top + paneH - ((v - lo) / tr.span) * paneH;

      // --- 窗格底 + 上下边界线 ---
      ctx.fillStyle = 'rgba(0,0,0,0.30)';
      ctx.fillRect(xL, top, xR - xL, paneH);
      ctx.strokeStyle = 'rgba(255,255,255,0.07)';
      ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(xL, top + 0.5); ctx.lineTo(xR, top + 0.5);
      ctx.moveTo(xL, top + paneH - 0.5); ctx.lineTo(xR, top + paneH - 0.5); ctx.stroke();
      // 窗格中位线
      ctx.strokeStyle = 'rgba(255,255,255,0.035)';
      ctx.setLineDash([3, 5]);
      ctx.beginPath(); ctx.moveTo(xL, top + paneH / 2); ctx.lineTo(xR, top + paneH / 2); ctx.stroke();
      ctx.setLineDash([]);

      // --- 时间网格（竖线，三个窗格共用同一套 x）---
      ctx.strokeStyle = 'rgba(255,255,255,0.045)';
      for (let i = 1; i < 8; i++) {
        const x = xL + (xR - xL) * i / 8;
        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + paneH); ctx.stroke();
      }

      if (tr.buf.length >= 2) {
        // --- 目标虚线：一眼看出有没有到位 ---
        const tv = this.targets[tr.key];
        if (Number.isFinite(tv)) {
          const y = yAt(tv);
          if (y > top && y < top + paneH) {
            ctx.strokeStyle = hexA(tr.color, 0.5);
            ctx.setLineDash([5, 4]);
            ctx.beginPath(); ctx.moveTo(xL, y); ctx.lineTo(xR, y); ctx.stroke();
            ctx.setLineDash([]);
          }
        }

        // --- 线下渐变填充（先画所有窗格的填充，再画线：顺序反了后面的填充会盖住前面的线）---
        const g = ctx.createLinearGradient(0, top, 0, top + paneH);
        g.addColorStop(0, hexA(tr.color, 0.20));
        g.addColorStop(1, hexA(tr.color, 0));
        ctx.beginPath();
        tr.buf.forEach((v, i) => { i ? ctx.lineTo(xAt(i), yAt(v)) : ctx.moveTo(xAt(i), yAt(v)); });
        ctx.lineTo(xR, top + paneH); ctx.lineTo(xL, top + paneH); ctx.closePath();
        ctx.fillStyle = g; ctx.fill();

        // --- 轨迹（带发光）---
        ctx.beginPath();
        tr.buf.forEach((v, i) => { i ? ctx.lineTo(xAt(i), yAt(v)) : ctx.moveTo(xAt(i), yAt(v)); });
        ctx.strokeStyle = tr.color;
        ctx.lineWidth = 1.6;
        ctx.shadowColor = tr.color;
        ctx.shadowBlur = 8;
        ctx.stroke();
        ctx.shadowBlur = 0;
      }

      // --- 窗格标题：名称 + 当前量程 + 实时值（放窗格内左上，不额外占高度）---
      const cur = tr.buf.length ? tr.buf[tr.buf.length - 1] : NaN;
      ctx.textAlign = 'left';
      ctx.font = '11px monospace';
      ctx.fillStyle = tr.color;
      ctx.fillText('■', xL + 4, top + 12);
      ctx.fillStyle = '#9BA1A9';
      const f = (v) => (Math.abs(v) < 10 ? v.toFixed(2) : v.toFixed(1));
      ctx.fillText(`${tr.label}  ${f(lo)}~${f(lo + tr.span)}`, xL + 18, top + 12);
      if (Number.isFinite(cur)) {
        ctx.textAlign = 'right';
        ctx.fillStyle = tr.color;
        ctx.fillText(cur.toFixed(tr.digits) + ' ' + tr.unit, xR - 4, top + 12);
      }
    });

    // --- 游标：竖线 + 各窗格该时刻的读数 ---
    if (this.cursor != null && this.cursor > xL && this.cursor < xR && nPts > 1) {
      const i = Math.round((this.cursor - xL) / (xR - xL) * (nPts - 1));
      const x = xAt(i);
      ctx.strokeStyle = 'rgba(255,255,255,0.35)';
      ctx.lineWidth = 1;
      ctx.setLineDash([2, 3]);
      ctx.beginPath(); ctx.moveTo(x, PAD.t); ctx.lineTo(x, h - PAD.b); ctx.stroke();
      ctx.setLineDash([]);

      this.traces.forEach((tr, k) => {
        const top = PAD.t + k * (paneH + gap);
        const v = tr.buf[i];
        if (!Number.isFinite(v)) return;
        const lo = tr.center - tr.span / 2;
        const y = top + paneH - ((v - lo) / tr.span) * paneH;
        ctx.beginPath();
        ctx.arc(x, Math.max(top + 3, Math.min(y, top + paneH - 3)), 3, 0, Math.PI * 2);
        ctx.fillStyle = tr.color; ctx.fill();
      });
    }

    // --- 时间轴 ---
    const secs = this.startTs ? ((this.lastTs - this.startTs) / 1000).toFixed(1) : '0.0';
    ctx.textAlign = 'right';
    ctx.font = '11px monospace';
    ctx.fillStyle = this.recording ? '#4ECB8E' : '#9BA1A9';
    ctx.fillText((this.recording ? '● 记录中 ' : '记录完成 ') + secs + 's', xR, h - 3);
    ctx.textAlign = 'left';
    ctx.fillStyle = '#666C75';
    ctx.fillText('t=0', xL, h - 3);
  }
}

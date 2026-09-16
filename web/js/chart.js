// 实时波形：Canvas 手绘三条轨迹。
//
// 与 Qt 端 CurvePanel 保持同样的取舍：
//  1) 平时不采样、不显示；点「下发目标」才开始记录本次响应
//  2) 每条轨迹独立自动缩放，但有**最小量程**——否则静止时的微小抖动会被
//     拉伸到满屏，看着像剧烈震荡（这个坑在 Qt 端踩过）
//  3) 量程平滑跟随，避免每帧重算导致波形整体跳动

const TRACES = [
  { key: 'positionDeg', color: '#4F8DF7', label: '位置', minSpan: 1.0 },
  { key: 'velocityDps', color: '#31D0AA', label: '速度', minSpan: 10.0 },
  { key: 'torqueNm',    color: '#F5A524', label: '力矩', minSpan: 1.0 },
];

const MAX_POINTS = 3000;   // 10s @300Hz 上限，够装下一次完整运动

class Chart {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.recording = false;
    this.traces = TRACES.map((t) => ({ ...t, buf: [], center: 0, span: 0, scaled: false }));
    this.moved = false;
    this.stillSince = 0;
    this.startTs = 0;
    this.lastTs = 0;

    // 高分屏清晰度
    const dpr = window.devicePixelRatio || 1;
    const resize = () => {
      const r = canvas.getBoundingClientRect();
      canvas.width = r.width * dpr;
      canvas.height = r.height * dpr;
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      this.draw();
    };
    new ResizeObserver(resize).observe(canvas);
    resize();
  }

  /** 点「下发目标」时调用：清空并开始记录本次响应 */
  start(ratedTorqueNm) {
    this.traces.forEach((t) => {
      t.buf = []; t.scaled = false;
      // 力矩最小量程按额定取 10%：驱动电流估算的力矩本身有约 1% 额定的纹波，
      // 固定量程会让大关节被这点纹波占满整屏
      if (t.key === 'torqueNm' && ratedTorqueNm > 0) t.minSpan = ratedTorqueNm * 0.1;
    });
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

  draw() {
    const ctx = this.ctx;
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    ctx.clearRect(0, 0, w, h);

    // 网格
    ctx.strokeStyle = '#1A1E24';
    ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const y = (h * i) / 4;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }

    const hasData = this.traces.some((t) => t.buf.length > 1);
    if (!hasData) {
      ctx.fillStyle = '#5A626C';
      ctx.font = '15px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('点「下发目标」后开始记录波形', w / 2, h / 2);
      return;
    }

    const pad = 10;
    this.traces.forEach((tr) => {
      if (tr.buf.length < 2) return;
      let lo = Math.min(...tr.buf), hi = Math.max(...tr.buf);
      const targetSpan = Math.max(tr.minSpan, (hi - lo) * 1.2);
      const targetCenter = (lo + hi) / 2;
      if (!tr.scaled) { tr.center = targetCenter; tr.span = targetSpan; tr.scaled = true; }
      else { tr.center += (targetCenter - tr.center) * 0.15; tr.span += (targetSpan - tr.span) * 0.15; }

      const loDisp = tr.center - tr.span / 2;
      const range = Math.max(1e-9, tr.span);
      const xSpan = Math.max(1, tr.buf.length - 1);

      ctx.strokeStyle = tr.color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      tr.buf.forEach((v, i) => {
        const x = (i / xSpan) * (w - 2 * pad) + pad;
        const y = h - pad - ((v - loDisp) / range) * (h - 2 * pad);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });

    // 图例 + 记录状态
    const secs = this.startTs ? ((this.lastTs - this.startTs) / 1000).toFixed(1) : '0.0';
    ctx.textAlign = 'left';
    ctx.font = '13px sans-serif';
    ctx.fillStyle = '#8A929C';
    let x = 12;
    this.traces.forEach((tr) => {
      ctx.fillStyle = tr.color;
      ctx.fillText('■', x, 20);
      ctx.fillStyle = '#8A929C';
      ctx.fillText(tr.label, x + 14, 20);
      x += 70;
    });
    ctx.fillStyle = this.recording ? '#31D0AA' : '#8A929C';
    ctx.fillText(`${this.recording ? '● 记录中' : '记录完成'} ${secs}s`, x + 10, 20);
  }
}

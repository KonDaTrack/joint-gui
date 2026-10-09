// 三个径向仪表：位置（±170°）/ 力矩（±额定）/ 速度（条形）。
//
// 为什么换成表盘而不是数字框：数字只给"是多少"，表盘还给"占了多少量程"。
// 力矩 3.2 N·m 在 9.6 额定的关节上已经用掉三分之一，在 50 额定的关节上几乎为零
// —— 光看数字分不出来，看填充弧一眼就知道。
//
// ---- 立体感是怎么做的 ----
// 不是靠阴影堆，是靠**凹槽**：表盘是一条**刻进面板的环形槽**，填充弧**嵌在槽里**。
// 光源统一从正上方来，所以：
//   · 槽底：竖直渐变「上暗下亮」—— 上半是槽壁挡出的阴影，下半受光。
//     这是"凹"的关键，反过来（上亮下暗）立刻变成"凸"。
//   · 槽沿：内外各一道细线（外沿暗、内沿亮），给出两条倒角边。
//   · 填充弧：比槽窄（7 vs 12），嵌在槽中间；自身也带上亮下暗的渐变 + 外发光。
//   · 游标：径向渐变画成球（高光偏左上）。
//   · 玻璃反光：顶部一段很淡的白色弧，像表镜上的反光。
//
// 量程都来自下位机（行程 ±170 是机械限位，额定力矩是遥测字段），换关节不用改。

const Gauges = (() => {
  const NS = 'http://www.w3.org/2000/svg';
  const A0 = -120, A1 = 120;      // 弧的起止角（0 = 正上方，顺时针为正）
  const CX = 100, CY = 104, R = 74;

  const el = (tag, attrs, text) => {
    const e = document.createElementNS(NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (text != null) e.textContent = text;
    return e;
  };
  const px = (r, deg) => CX + r * Math.sin(deg * Math.PI / 180);
  const py = (r, deg) => CY - r * Math.cos(deg * Math.PI / 180);
  const arc = (r, a0, a1) => {
    const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
    const sweep = a1 > a0 ? 1 : 0;
    return `M${px(r, a0)} ${py(r, a0)} A${r} ${r} 0 ${large} ${sweep} ${px(r, a1)} ${py(r, a1)}`;
  };

  let uidSeq = 0;
  const darken = (hex, k) => {
    const n = parseInt(hex.slice(1), 16);
    const f = (v) => Math.round(Math.min(255, v * k));
    return `rgb(${f(n >> 16 & 255)},${f(n >> 8 & 255)},${f(n & 255)})`;
  };
  const lighten = (hex, k) => {
    const n = parseInt(hex.slice(1), 16);
    const f = (v) => Math.round(Math.min(255, v + (255 - v) * k));
    return `rgb(${f(n >> 16 & 255)},${f(n >> 8 & 255)},${f(n & 255)})`;
  };

  /**
   * 建一个弧表。
   * @param {object} o { min, max, unit, ticks:[值…], labels:[值…], color }
   */
  function dial(o) {
    const uid = 'gd' + (++uidSeq);
    const base = o.color || '#78D6EE';
    const svg = el('svg', { viewBox: '0 0 200 158', class: 'g-dial' });

    /* ---- defs：整张表的"光源"都定义在这里，改一处全局一致 ---- */
    const defs = el('defs', {});
    // 槽底：上暗下亮 = 凹
    defs.innerHTML = `
      <linearGradient id="${uid}-groove" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0"   stop-color="#040506"/>
        <stop offset="0.55" stop-color="#0E1114"/>
        <stop offset="1"   stop-color="#2A2F36"/>
      </linearGradient>
      <linearGradient id="${uid}-fill" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0"   stop-color="${lighten(base, 0.45)}"/>
        <stop offset="0.5" stop-color="${base}"/>
        <stop offset="1"   stop-color="${darken(base, 0.62)}"/>
      </linearGradient>
      <radialGradient id="${uid}-dot" cx="0.34" cy="0.28" r="0.85">
        <stop offset="0"   stop-color="#FFFFFF"/>
        <stop offset="0.4" stop-color="${lighten(base, 0.35)}"/>
        <stop offset="1"   stop-color="${darken(base, 0.55)}"/>
      </radialGradient>
      <radialGradient id="${uid}-well" cx="0.5" cy="0.42" r="0.72">
        <stop offset="0"   stop-color="rgba(255,255,255,0.035)"/>
        <stop offset="0.7" stop-color="rgba(0,0,0,0)"/>
        <stop offset="1"   stop-color="rgba(0,0,0,0.35)"/>
      </radialGradient>`;
    svg.appendChild(defs);

    // 表盘底：中心微亮、边缘压暗 → 一块微凹的表盘面
    svg.appendChild(el('circle', { cx: CX, cy: CY, r: R + 16, fill: `url(#${uid}-well)` }));

    // 槽外沿（暗）→ 槽底（渐变）→ 槽内沿（亮）：三条同心弧叠出倒角
    svg.appendChild(el('path', { d: arc(R + 6, A0, A1), class: 'g-edge-out' }));
    svg.appendChild(el('path', { d: arc(R, A0, A1), class: 'g-groove',
                                stroke: `url(#${uid}-groove)` }));
    svg.appendChild(el('path', { d: arc(R - 6, A0, A1), class: 'g-edge-in' }));

    // 填充弧（嵌在槽里，比槽窄）+ 自身发光
    const fill = el('path', { d: '', class: 'g-fill', stroke: `url(#${uid}-fill)` });
    svg.appendChild(fill);

    // 0 位刻线
    svg.appendChild(el('line', {
      x1: px(R - 10, 0), y1: py(R - 10, 0), x2: px(R + 8, 0), y2: py(R + 8, 0), class: 'g-zero',
    }));

    // 刻度
    (o.ticks || []).forEach((v) => {
      const a = o.angle(v);
      const major = (o.labels || []).includes(v);
      const r2 = R + (major ? 9 : 5);
      svg.appendChild(el('line', {
        x1: px(R + (major ? 8 : 7), a), y1: py(R + (major ? 8 : 7), a),
        x2: px(r2, a), y2: py(r2, a),
        class: major ? 'g-tick-major' : 'g-tick',
      }));
      if (major) {
        svg.appendChild(el('text', {
          x: px(R + 19, a), y: py(R + 19, a) + 3.5,
          class: 'g-tick-label', 'text-anchor': 'middle',
        }, String(v)));
      }
    });

    // 游标球
    const dot = el('circle', { cx: -99, cy: -99, r: 6, fill: `url(#${uid}-dot)`, class: 'g-dot' });
    svg.appendChild(dot);

    // 玻璃反光：顶部一段很淡的弧
    svg.appendChild(el('path', { d: arc(R + 13, -58, 58), class: 'g-glass' }));

    // 中间的大数字
    const num = el('text', { x: CX, y: CY - 6, class: 'g-num', 'text-anchor': 'middle' }, '--');
    const unit = el('text', { x: CX, y: CY + 16, class: 'g-unit', 'text-anchor': 'middle' }, o.unit);
    svg.appendChild(num);
    svg.appendChild(unit);

    return {
      svg,
      set(v) {
        if (!Number.isFinite(v)) {
          num.textContent = '--'; fill.setAttribute('d', '');
          dot.setAttribute('cx', -99); dot.setAttribute('cy', -99);
          return;
        }
        const a = o.angle(v);
        fill.setAttribute('d', v === 0 ? '' : arc(R, 0, a));
        dot.setAttribute('cx', px(R, a));
        dot.setAttribute('cy', py(R, a));
        num.textContent = o.fmt(v);
      },
    };
  }

  /** 线性映射 —— 两个表盘只是量程和颜色不同 */
  function range(min, max, unit, decimals) {
    const angle = (v) => A0 + (Math.min(Math.max(v, min), max) - min) / (max - min) * (A1 - A0);
    const fmt = (v) => (v >= 0 ? '+' : '') + v.toFixed(decimals);
    return { min, max, unit, angle, fmt };
  }

  let posG, torG;
  const stateRated = { v: 0 };
  let lastTorque = NaN;   // 重建力矩表盘后要把当前值补回去，否则会空一帧


  const span = (a, b, step) => {
    const out = [];
    for (let v = a; v <= b + 1e-9; v += step) out.push(Math.round(v * 100) / 100);
    return out;
  };

  function mount() {
    const posBox = document.getElementById('gaugePos');
    const torBox = document.getElementById('gaugeTor');
    const velBox = document.getElementById('gaugeVel');
    if (!posBox || !torBox || !velBox) return;

    posG = dial(Object.assign(range(-170, 170, 'deg', 2),
      { ticks: span(-170, 170, 20), labels: [-170, -90, 0, 90, 170], color: '#78D6EE' }));
    torG = dial(Object.assign(range(-1, 1, 'N·m', 2),
      { ticks: span(-1, 1, 0.25), labels: [-1, 0, 1], color: '#D9A441' }));

    posBox.appendChild(posG.svg);
    torBox.appendChild(torG.svg);
    velBox.appendChild(buildVelBar());
  }

  /* ---- 速度：中心零点的横向条 ---- */
  let velFill, velNum;
  function buildVelBar() {
    const wrap = document.createElement('div');
    wrap.className = 'g-bar';
    wrap.innerHTML = `
      <div class="g-bar-head"><span>速度</span><span class="v" id="gVelVal">--</span></div>
      <div class="g-bar-track">
        <div class="g-bar-fill" id="gVelFill"></div>
        <div class="g-bar-zero"></div>
      </div>`;
    velFill = wrap.querySelector('#gVelFill');
    velNum  = wrap.querySelector('#gVelVal');
    return wrap;
  }

  return {
    mount,

    /** 额定力矩来自下位机 —— 换关节时表盘量程跟着变，前端不用改 */
    setRated(nm) {
      if (!Number.isFinite(nm) || nm <= 0 || nm === stateRated.v) return;
      stateRated.v = nm;
      const box = document.getElementById('gaugeTor');
      if (!box || !torG) return;
      const g = dial(Object.assign(range(-nm, nm, 'N·m', 2),
        { ticks: span(-nm, nm, nm / 4), labels: [-nm, 0, nm], color: '#D9A441' }));
      box.replaceChildren(g.svg);
      torG = g;
      // 重建后要把当前值补回去，否则会空一帧
      if (Number.isFinite(lastTorque)) torG.set(lastTorque);
    },

    /** @param {object} t 该轴遥测 */
    update(t) {
      if (!t) return;
      this.setRated(t.ratedTorqueNm);
      lastTorque = t.torqueNm;
      if (posG) posG.set(t.positionDeg);
      if (torG) torG.set(t.torqueNm);
      if (velFill) {
        const v = Number.isFinite(t.velocityDps) ? t.velocityDps : 0;
        const velMax = Math.max(60, Math.abs(v) * 1.2);
        velFill.style.width = Math.min(50, Math.abs(v) / velMax * 50) + '%';
        velFill.classList.toggle('neg', v < 0);
        velNum.innerHTML = (v >= 0 ? '+' : '') + v.toFixed(1) + ' <span class="u">deg/s</span>';
      }
    },
  };
})();

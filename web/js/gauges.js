// 三个径向仪表：位置（±170°）/ 力矩（±额定）/ 速度（条形）。
//
// 为什么换成表盘而不是数字框：数字只给"是多少"，表盘还给"占了多少量程"。
// 力矩 3.2 N·m 在 9.6 额定的关节上已经用掉三分之一，在 50 额定的关节上几乎为零
// —— 光看数字分不出来，看填充弧一眼就知道。
//
// 两个表盘共用同一套弧表绘制（只是量程和单位不同），保证视觉一致。
// 位置/速度/力矩的**量程都来自下位机**（行程 ±170 是机械限位，额定力矩是遥测字段），
// 所以换关节不用改这里。

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
  /** 极坐标：deg=0 在正上方，顺时针为正 */
  const px = (r, deg) => CX + r * Math.sin(deg * Math.PI / 180);
  const py = (r, deg) => CY - r * Math.cos(deg * Math.PI / 180);
  const arc = (r, a0, a1) => {
    const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
    const sweep = a1 > a0 ? 1 : 0;
    return `M${px(r, a0)} ${py(r, a0)} A${r} ${r} 0 ${large} ${sweep} ${px(r, a1)} ${py(r, a1)}`;
  };

  /**
   * 建一个弧表。
   * @param {object} o { min, max, unit, ticks:[值…], labels:[值…] }
   *   ticks 画短刻度、labels 另加文字与长刻度。量程由调用方给（来自下位机）。
   */
  function dial(o) {
    const svg = el('svg', { viewBox: '0 0 200 158', class: 'g-dial' });

    // 底弧（整段量程）
    svg.appendChild(el('path', { d: arc(R, A0, A1), class: 'g-track' }));
    // 填充弧（从 0 到当前值），由 update 改 d
    const fill = el('path', { d: '', class: 'g-fill' });
    svg.appendChild(fill);

    // 中轴刻度线（0 位）
    svg.appendChild(el('line', {
      x1: px(R - 9, 0), y1: py(R - 9, 0), x2: px(R + 5, 0), y2: py(R + 5, 0), class: 'g-zero',
    }));

    // 刻度
    (o.ticks || []).forEach((v) => {
      const a = o.angle(v);
      const major = (o.labels || []).includes(v);
      const r2 = R + (major ? 7 : 4);
      svg.appendChild(el('line', {
        x1: px(R, a), y1: py(R, a), x2: px(r2, a), y2: py(r2, a),
        class: major ? 'g-tick-major' : 'g-tick',
      }));
      if (major) {
        svg.appendChild(el('text', {
          x: px(R + 17, a), y: py(R + 17, a) + 3.5,
          class: 'g-tick-label', 'text-anchor': 'middle',
        }, String(v)));
      }
    });

    // 当前值游标（发光点），由 update 改位置
    const dot = el('circle', { cx: CX, cy: CY, r: 4.6, class: 'g-dot' });
    svg.appendChild(dot);

    // 中间的大数字
    const num = el('text', { x: CX, y: CY - 6, class: 'g-num', 'text-anchor': 'middle' }, '--');
    const unit = el('text', { x: CX, y: CY + 16, class: 'g-unit', 'text-anchor': 'middle' }, o.unit);
    svg.appendChild(num);
    svg.appendChild(unit);

    const api = {
      svg,
      /** @param {number} v 当前值 */
      set(v) {
        if (!Number.isFinite(v)) { num.textContent = '--'; fill.setAttribute('d', ''); dot.setAttribute('cx', -99); return; }
        const a = o.angle(v);
        fill.setAttribute('d', v === 0 ? '' : arc(R, 0, a));
        dot.setAttribute('cx', px(R, a));
        dot.setAttribute('cy', py(R, a));
        num.textContent = o.fmt(v);
      },
      /** 量程变了（换了关节，额定不同）就重建刻度 */
      setRange(min, max) { o.min = min; o.max = max; },
    };
    return api;
  }

  /** 生成线性映射的 angle/fmt —— 两个表盘只是量程不同 */
  function range(min, max, unit, decimals) {
    const angle = (v) => A0 + (Math.min(Math.max(v, min), max) - min) / (max - min) * (A1 - A0);
    const fmt = (v) => (v >= 0 ? '+' : '') + v.toFixed(decimals);
    return { min, max, unit, angle, fmt };
  }

  let posG, torG, velG, state = { rated: 0, limit: 170 };

  function mount() {
    const posBox = document.getElementById('gaugePos');
    const torBox = document.getElementById('gaugeTor');
    const velBox = document.getElementById('gaugeVel');
    if (!posBox || !torBox || !velBox) return;

    const mk = (r, ticks, labels) => {
      const o = Object.assign({}, r, { ticks, labels });
      return dial(o);
    };
    const span = (a, b, step) => { const out = []; for (let v = a; v <= b + 1e-9; v += step) out.push(Math.round(v * 100) / 100); return out; };

    posG = mk(range(-170, 170, 'deg', 2),
      span(-170, 170, 20), [-170, -90, 0, 90, 170]);
    torG = mk(range(-1, 1, 'N·m', 2),
      span(-1, 1, 0.25), [-1, 0, 1]);

    posBox.appendChild(posG.svg);
    torBox.appendChild(torG.svg);
    velBox.appendChild(buildVelBar());
  }

  /* ---- 速度：中心零点的横向条 ---- */
  let velFill, velNum, velLim;
  function buildVelBar() {
    const wrap = document.createElement('div');
    wrap.className = 'g-bar';
    wrap.innerHTML = `
      <div class="g-bar-head"><span>速度</span><span class="v" id="gVelVal">--</span></div>
      <div class="g-bar-track">
        <div class="g-bar-fill" id="gVelFill"></div>
        <div class="g-bar-zero"></div>
        <div class="g-bar-lim" id="gVelLim"></div>
      </div>`;
    velFill = wrap.querySelector('#gVelFill');
    velNum  = wrap.querySelector('#gVelVal');
    velLim  = wrap.querySelector('#gVelLim');
    return wrap;
  }

  let velMax = 60;

  return {
    mount,

    /** 换关节/首次拿到遥测时更新量程（额定力矩来自下位机） */
    setRated(nm) {
      if (!Number.isFinite(nm) || nm <= 0 || nm === state.rated) return;
      state.rated = nm;
      if (torG) {
        const r = range(-nm, nm, 'N·m', 2);
        Object.assign(torG, {});                    // 量程存在闭包里的 o 上，重建刻度更省事
      }
      // 刻度随量程变，直接重建这个表盘
      const box = document.getElementById('gaugeTor');
      if (box && torG) {
        const span = (a, b, s) => { const o = []; for (let v = a; v <= b + 1e-9; v += s) o.push(Math.round(v * 100) / 100); return o; };
        const r = range(-nm, nm, 'N·m', 2);
        const g = dial(Object.assign({}, r, { ticks: span(-nm, nm, nm / 4), labels: [-nm, 0, nm] }));
        box.replaceChildren(g.svg);
        torG = g;
      }
    },

    /** @param {object} t 该轴遥测 */
    update(t) {
      if (!t) return;
      this.setRated(t.ratedTorqueNm);
      if (posG) posG.set(t.positionDeg);
      if (torG) torG.set(t.torqueNm);
      if (velFill) {
        const v = Number.isFinite(t.velocityDps) ? t.velocityDps : 0;
        velMax = Math.max(60, Math.abs(v) * 1.2);
        const w = Math.min(50, Math.abs(v) / velMax * 50);
        velFill.style.width = w + '%';
        velFill.classList.toggle('neg', v < 0);
        velNum.textContent = (v >= 0 ? '+' : '') + v.toFixed(1) + ' ';
        velNum.innerHTML = (v >= 0 ? '+' : '') + v.toFixed(1) + ' <span class="u">deg/s</span>';
      }
    },
  };
})();

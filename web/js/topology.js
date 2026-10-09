// 系统拓扑与信号走向（组态页的核心）。
//
// 拓扑按实际接线画，**不编**：
//
//   主干（横向）  PC ══WebSocket══► ARM ══EtherCAT══► 关节1 ─► 关节2 ─► 关节3
//   RS485（竖）   ARM ──RS485──► 三路模拟输出模块
//   负载链（每关节一列，与关节同列对齐）
//                 模拟输出 ──► 负载控制器N ──► 负载N ──机械耦合──► 关节N
//
// 三条设计取舍：
//  1) **严格行列网格**：负载链的每一列都与它对应的关节同 x 对齐 ——
//     对齐本身就是信息（"这个负载管的是这个关节"），错开就看不出来了。
//  2) **节点用图形不用文字**：显示器认得出是 PC、排针芯片认得出是开发板。
//     图先说话，名称只是补充，压在图形下方小字。
//  3) **连线做成线缆**：深色护套 + 细的信号芯 + 两端接头。
//
// ⚠️ **协议只有一路负载**（setLoad / loadState 都是单值），而硬件是三路。
//    所以只有第 1 列显示真实负载，其余标注"未接入"——不假装它们可控。
//
// 表现层只做"显示"，不做任何安全判断。

const Topology = {
  svg: null, wrap: null,
  _links: [], _joints: [],
  _active: false, _t0: 0, _raf: null,
  _hoverSlave: null,

  /* ================= 几何：严格行列网格 =================
     viewBox 1460×660（起点 30,40）—— 宽高比 2.21，与组态卡（约 2.19）基本吻合，
     所以能铺满、不留大片空白。 */
  VB: { x: 30, y: 40, w: 1460, h: 660 },
  PC:    { cx: 118, cy: 130 },
  ARM:   { cx: 404, cy: 130 },
  AO:    { cx: 404, cy: 596 },     // 三路模拟输出模块（ARM 正下方）
  ROW:   { joint: 130, load: 330, ctrl: 462, bus: 596 },
  COL:   { x0: 770, gap: 258 },    // 关节/负载/控制器**共用**的列坐标

  colX(i) { return this.COL.x0 + i * this.COL.gap; },

  NS: 'http://www.w3.org/2000/svg',
  _el(tag, attrs, text) {
    const e = document.createElementNS(this.NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (text != null) e.textContent = text;
    return e;
  },

  /* ================= 挂载 ================= */
  mount(container) {
    if (!container) return;
    container.innerHTML = '';
    this.wrap = container;

    // 悬停参数浮层用 HTML 而不是 SVG：文字排版、边框、阴影复用现有卡片语汇。
    // 由这里创建而不是写在 index.html 里 —— mount 会清空容器。
    const detail = document.createElement('div');
    detail.className = 'tp-detail';
    detail.hidden = true;
    container.appendChild(detail);

    const svg = this._el('svg', {
      viewBox: `${this.VB.x} ${this.VB.y} ${this.VB.w} ${this.VB.h}`,
      class: 'topo-svg',
    });
    this.svg = svg;

    // 图层：线缆 → 数据包 → 节点
    this.gCables  = this._el('g', { class: 'topo-cables' });
    this.gPackets = this._el('g', { class: 'topo-packets' });
    this.gNodes   = this._el('g', { class: 'topo-nodes' });
    svg.append(this.gCables, this.gPackets, this.gNodes);
    container.appendChild(svg);

    this._buildStatic();
    this._buildChain([]);
  },

  /* ================= 静态节点：PC / ARM / 模拟输出 ================= */

  _buildStatic() {
    const P = this.PC, A = this.ARM, M = this.AO;

    // ---- PC 上位机：显示器 + 主机塔 ----
    const pc = this._el('g', { class: 'tp-node tp-node-pc' });
    pc.append(
      this._el('rect', { x: P.cx - 68, y: P.cy - 28, width: 28, height: 62, rx: 4, class: 'tp-metal' }),
      this._el('rect', { x: P.cx - 63, y: P.cy - 20, width: 18, height: 3, rx: 1.5, class: 'tp-slot' }),
      this._el('rect', { x: P.cx - 63, y: P.cy - 12, width: 18, height: 3, rx: 1.5, class: 'tp-slot' }),
      this._el('circle', { cx: P.cx - 54, cy: P.cy + 22, r: 2.6, class: 'tp-led' }),
      this._el('rect', { x: P.cx - 30, y: P.cy - 42, width: 88, height: 62, rx: 5, class: 'tp-screen-bezel' }),
      this._el('rect', { x: P.cx - 25, y: P.cy - 37, width: 78, height: 52, rx: 3, class: 'tp-screen' }),
      this._el('rect', { x: P.cx + 4,  y: P.cy + 20, width: 22, height: 8, class: 'tp-metal' }),
      this._el('rect', { x: P.cx - 8,  y: P.cy + 28, width: 46, height: 5, rx: 2.5, class: 'tp-metal' }),
    );
    const wave = [];
    for (let i = 0; i <= 20; i++) {
      wave.push(`${P.cx - 22 + i * 3.6} ${P.cy - 20 + Math.sin(i * 0.8) * 8}`);
    }
    pc.appendChild(this._el('polyline', { points: wave.join(' '), class: 'tp-screen-wave' }));
    this.gNodes.appendChild(pc);
    this._label(P.cx, P.cy + 60, 'PC 上位机', 'tp-name');
    this.pcSub = this._label(P.cx, P.cy + 78, '', 'tp-sub');

    // ---- ARM 板：PCB + 芯片 + 排针 ----
    const arm = this._el('g', { class: 'tp-node tp-node-arm' });
    arm.append(
      this._el('rect', { x: A.cx - 84, y: A.cy - 54, width: 168, height: 108, rx: 8, class: 'tp-pcb' }),
      this._el('rect', { x: A.cx - 58, y: A.cy - 26, width: 48, height: 44, rx: 3, class: 'tp-chip' }),
      this._el('rect', { x: A.cx - 52, y: A.cy - 20, width: 36, height: 32, rx: 2, class: 'tp-chip-die' }),
      this._el('rect', { x: A.cx + 2, y: A.cy - 32, width: 32, height: 20, rx: 2, class: 'tp-chip' }),
      this._el('rect', { x: A.cx + 2, y: A.cy - 6,  width: 32, height: 20, rx: 2, class: 'tp-chip' }),
      this._el('rect', { x: A.cx + 42, y: A.cy - 32, width: 24, height: 48, rx: 2, class: 'tp-chip' }),
      this._el('rect', { x: A.cx - 78, y: A.cy + 32, width: 28, height: 16, rx: 2, class: 'tp-jack' }),
      this._el('rect', { x: A.cx + 48, y: A.cy + 32, width: 28, height: 16, rx: 2, class: 'tp-jack' }),
    );
    for (let i = 0; i < 15; i++) {
      const x = A.cx - 72 + i * 10;
      arm.append(
        this._el('rect', { x, y: A.cy - 62, width: 3, height: 9, rx: 1, class: 'tp-pin' }),
        this._el('rect', { x, y: A.cy + 53, width: 3, height: 9, rx: 1, class: 'tp-pin' }),
      );
    }
    this.gNodes.appendChild(arm);
    this._label(A.cx, A.cy + 80, 'ARM 板 · 下位机', 'tp-name');
    this.armSub = this._label(A.cx, A.cy + 98, '', 'tp-sub');

    // ---- 三路模拟输出模块：机壳 + 三个输出端子 + 三个指示灯 ----
    const ao = this._el('g', { class: 'tp-node tp-node-ao' });
    ao.append(
      this._el('rect', { x: M.cx - 70, y: M.cy - 38, width: 140, height: 76, rx: 7, class: 'tp-module' }),
      this._el('rect', { x: M.cx - 62, y: M.cy - 28, width: 50, height: 22, rx: 3, class: 'tp-module-label' }),
    );
    for (let i = 0; i < 3; i++) {
      const y = M.cy - 18 + i * 18;
      ao.append(
        this._el('circle', { cx: M.cx - 4, cy: y, r: 3, class: 'tp-module-led' }),
        this._el('rect', { x: M.cx + 20, y: y - 6, width: 20, height: 12, rx: 2, class: 'tp-terminal' }),
        this._el('text', { x: M.cx + 30, y: y + 3.5, class: 'tp-terminal-n', 'text-anchor': 'middle' }, String(i + 1)),
      );
    }
    this.gNodes.appendChild(ao);
    this._label(M.cx, M.cy + 58, '三路模拟输出', 'tp-name');
    this.aoSub = this._label(M.cx, M.cy + 76, 'RS485 · Modbus', 'tp-sub');

    // ---- 静态线缆 ----
    // WebSocket：PC → ARM
    this._cable('ws', `M${P.cx + 60} ${P.cy} L${A.cx - 90} ${A.cy}`,
      { label: 'WebSocket', sub: '遥测 30~60Hz ↓ · 命令 ↑ · 心跳 200ms' });
    // RS485：ARM ↓ 模拟输出（ARM 正下方，竖向）
    this._cable('rs485', `M${A.cx} ${A.cy + 64} L${M.cx} ${M.cy - 40}`,
      { label: 'RS485 → Modbus', sub: '与 EtherCAT 独立 · 只受控制权约束', side: 'right' });
  },

  _label(x, y, text, cls) {
    const t = this._el('text', { x, y, class: cls, 'text-anchor': 'middle' }, text);
    this.gNodes.appendChild(t);
    return t;
  },

  /* ================= 关节链（动态）：每关节一列，含它的负载与控制器 ================= */

  _buildChain(slaves) {
    this._joints.forEach((j) => j.g.remove());
    this._joints = [];
    // ★ 按**前缀**清，不能只清 'mech' —— 机械耦合是按列编号的（mech1/mech2/…），
    //   重建时只清 'mech' 会让旧的那批留在图上叠加（踩过：mech1 出现两次）。
    this._clearLink('ecat');
    ['mech', 'cas', 'ch', 'ctrl'].forEach((p) => this._clearLinksByPrefix(p));

    const R = this.ROW;
    const list = slaves.length ? slaves : [null];

    // 关节多时压缩列距，保证整条链还在画布内
    const maxSpan = (this.VB.x + this.VB.w - 120) - this.COL.x0;
    const gap = list.length > 1 ? Math.min(this.COL.gap, maxSpan / (list.length - 1)) : this.COL.gap;
    const colX = (i) => this.COL.x0 + i * gap;

    // 主干：ARM → 关节1
    this._cable('ecat', `M${this.ARM.cx + 90} ${R.joint} L${colX(0) - 62} ${R.joint}`,
      { label: 'EtherCAT', sub: '主站 ↔ 从站 · 可级联' });

    list.forEach((s, i) => {
      const cx = colX(i);
      const g = this._el('g', { class: 'tp-node tp-joint' });

      /* ---- 关节：谐波减速器意象环 ---- */
      const ring = this._el('circle', { cx, cy: R.joint, r: 50, class: 'tp-j-ring' });
      const spinF = this._el('g', { class: 'tp-j-spin-flex', style: `transform-origin:${cx}px ${R.joint}px` });
      spinF.appendChild(this._el('circle', { cx, cy: R.joint, r: 35, class: 'tp-j-flex' }));
      const spinW = this._el('g', { class: 'tp-j-spin-wave', style: `transform-origin:${cx}px ${R.joint}px` });
      spinW.appendChild(this._el('ellipse', { cx, cy: R.joint, rx: 22, ry: 15, class: 'tp-j-wave' }));
      g.append(
        ring,
        this._el('circle', { cx, cy: R.joint, r: 44, class: 'tp-j-teeth' }),
        spinF, spinW,
        this._el('circle', { cx, cy: R.joint, r: 4.5, class: 'tp-j-hub' }),
      );
      g.append(this._label(cx, R.joint + 74, s ? `关节 ${s.slave}` : '无关节', 'tp-name'));
      const sub = this._label(cx, R.joint + 92, s ? (s.shortName || '--') : '--', 'tp-sub');

      /* ---- 负载（磁粉制动器）：与关节同列 ---- */
      const lc = this._el('g', { class: 'tp-node tp-load' });
      lc.append(
        this._el('circle', { cx: cx - 20, cy: R.load, r: 32, class: 'tp-disc' }),
        this._el('circle', { cx: cx - 20, cy: R.load, r: 20, class: 'tp-disc-inner' }),
        this._el('circle', { cx: cx - 20, cy: R.load, r: 6,  class: 'tp-disc-hub' }),
        this._el('rect', { x: cx + 4, y: R.load - 26, width: 30, height: 52, rx: 5, class: 'tp-caliper' }),
      );
      for (let k = -2; k <= 2; k++) {
        lc.append(this._el('rect', { x: cx + 4, y: R.load + k * 9 - 1.5, width: 30, height: 3, rx: 1.5, class: 'tp-caliper-vent' }));
      }
      g.appendChild(lc);
      g.append(this._label(cx, R.load + 54, `负载 ${i + 1}`, 'tp-name'));

      /* ---- 负载控制器：与关节同列 ---- */
      const ct = this._el('g', { class: 'tp-node tp-ctrl' });
      ct.append(
        this._el('rect', { x: cx - 46, y: R.ctrl - 32, width: 92, height: 64, rx: 6, class: 'tp-module' }),
        this._el('rect', { x: cx - 36, y: R.ctrl - 20, width: 42, height: 18, rx: 3, class: 'tp-module-label' }),
        this._el('circle', { cx: cx + 22, cy: R.ctrl - 2, r: 11, class: 'tp-knob' }),
        this._el('circle', { cx: cx + 22, cy: R.ctrl - 2, r: 3,  class: 'tp-knob-mark' }),
      );
      g.appendChild(ct);
      g.append(this._label(cx, R.ctrl + 52, `负载控制器 ${i + 1}`, 'tp-name'));

      /* ---- 悬停命中区（覆盖关节及其文字） ---- */
      const hit = this._el('rect', { x: cx - 62, y: R.joint - 62, width: 124, height: 168, class: 'tp-hit', rx: 10 });
      g.insertBefore(hit, g.firstChild);
      if (s) {
        g.addEventListener('mouseenter', () => this._showDetail(s.slave, cx, R.joint));
        g.addEventListener('mouseleave', () => this._hideDetail());
      }

      this.gNodes.appendChild(g);
      this._joints.push({ g, slave: s ? s.slave : null, cx, ring, sub,
                          loadSub: g.querySelector('.tp-sub:last-of-type') });

      /* ---- 线缆 ---- */
      if (i > 0) {
        this._cable('cas' + i, `M${colX(i - 1) + 62} ${R.joint} L${cx - 62} ${R.joint}`, { label: '', sub: '' });
      }
      // 模拟输出 → 控制器（总线在 bus 行，到本列再折上去）
      this._cable('ch' + i, `M${this.AO.cx + 72} ${R.bus} L${cx} ${R.bus} L${cx} ${R.ctrl + 34}`,
        { label: '', sub: '' });
      // 控制器 → 负载
      this._cable('ctrl' + i, `M${cx} ${R.ctrl - 34} L${cx} ${R.load + 34}`, { label: '', sub: '' });
      // 负载 → 关节（机械耦合）
      this._cable('mech' + (i + 1), `M${cx} ${R.load - 34} L${cx} ${R.joint + 52}`,
        { label: i === 0 ? '机械耦合' : '', sub: i === 0 ? '咬住被测轴' : '',
          dashed: true, flow: false, side: 'left', labelDx: -66 });
    });
  },

  setSlaves(slaves) {
    if (!this.svg) return;
    this._hideDetail();
    this._buildChain(slaves || []);
  },

  /* ================= 线缆 ================= */
  _cable(id, d, opts) {
    const g = this._el('g', { class: 'tp-cable', 'data-cable': id });
    const sheath = this._el('path', { d, class: 'tp-sheath' + (opts.dashed ? ' tp-dash' : '') });
    const core   = this._el('path', { d, class: 'tp-core' });
    g.append(sheath, core);

    try {
      const len = sheath.getTotalLength();
      [0, 1].forEach((end) => {
        const p = sheath.getPointAtLength(end ? len : 0);
        const q = sheath.getPointAtLength(end ? Math.max(0, len - 14) : Math.min(len, 14));
        const ang = Math.atan2(q.y - p.y, q.x - p.x) * 180 / Math.PI;
        g.appendChild(this._el('rect', {
          x: p.x - 5, y: p.y - 5, width: 10, height: 10, rx: 2,
          transform: `rotate(${ang} ${p.x} ${p.y})`, class: 'tp-plug',
        }));
      });
    } catch (e) { /* 极短路径上 getTotalLength 可能抛，忽略 */ }

    if (opts.label) {
      const len = sheath.getTotalLength();
      const mid = sheath.getPointAtLength(len * 0.5);
      const anchor = opts.side === 'left' ? 'end' : 'start';
      // labelDx 可覆盖默认偏移：默认 ±14 贴着线，但竖线的中段往往正好是
      // 节点标签所在的高度，贴太近会挤在一起（机械耦合那根踩过）。
      const dx = opts.labelDx != null ? opts.labelDx : (opts.side === 'left' ? -14 : 14);
      g.appendChild(this._el('text', { x: mid.x + dx, y: mid.y - 6, class: 'tp-label', 'text-anchor': anchor }, opts.label));
      if (opts.sub) {
        g.appendChild(this._el('text', { x: mid.x + dx, y: mid.y + 12, class: 'tp-sublabel', 'text-anchor': anchor }, opts.sub));
      }
    }

    this.gCables.appendChild(g);
    const len = sheath.getTotalLength();
    const entry = { id, g, path: sheath, len, flow: opts.flow !== false, packets: [] };
    if (entry.flow) {
      const n = id === 'ws' ? 4 : id === 'rs485' ? 2 : (id.startsWith('cas') || id.startsWith('ctrl')) ? 2 : 3;
      for (let i = 0; i < n; i++) {
        const c = this._el('circle', { r: 3.2, class: 'tp-packet tp-packet-' + id });
        this.gPackets.appendChild(c);
        entry.packets.push({ el: c, phase: i / n });
      }
    }
    this._links.push(entry);
    return entry;
  },

  _clearLink(id) {
    const i = this._links.findIndex((l) => l.id === id);
    if (i < 0) return;
    const l = this._links[i];
    l.g.remove();
    l.packets.forEach((p) => p.el.remove());
    this._links.splice(i, 1);
  },
  _clearLinksByPrefix(prefix) {
    this._links.filter((l) => l.id.startsWith(prefix)).map((l) => l.id)
      .forEach((id) => this._clearLink(id));
  },
  _getLink(id) { return this._links.find((l) => l.id === id); },

  /* ================= 悬停参数浮层 ================= */
  _showDetail(slave, cx, cy) {
    this._hoverSlave = slave;
    this._renderDetail(slave, cx, cy);
  },
  _hideDetail() {
    this._hoverSlave = null;
    const el = this.wrap && this.wrap.querySelector('.tp-detail');
    if (el) el.hidden = true;
  },

  _renderDetail(slave, cx, cy) {
    const el = this.wrap && this.wrap.querySelector('.tp-detail');
    if (!el) return;
    const s = (this._st ? this._st.slaves : []).find((x) => x.slave === slave);
    const t = this._st && this._st.telemetry.get(slave);
    const fault = !!(t && (t.errorCode || t.driveState === 7));
    const hex = (v) => '0x' + v.toString(16).padStart(4, '0');

    const rows = [
      ['从站号',   `#${slave}`, ''],
      ['型号',     (s && s.model) || '--', ''],
      ['额定力矩', t && t.ratedTorqueNm ? t.ratedTorqueNm.toFixed(1) + ' N·m' : '--', ''],
      ['驱动状态', t ? (DRIVE_STATES[t.driveState] ?? '--') : '--', fault ? 'dg' : ''],
      ['状态字',   t ? hex(t.statusWord) : '--', ''],
      ['故障码',   t ? (t.errorCode ? hex(t.errorCode) : '无') : '--', fault ? 'dg' : ''],
      ['温度',     t && t.temperatureC > 0 ? t.temperatureC.toFixed(1) + ' ℃' : 'N/A', ''],
      ['限位状态', t ? (t.limitExceeded ? '越限' : '正常') : '--', t && t.limitExceeded ? 'dg' : ''],
    ];
    el.innerHTML = `<div class="hd">关节 ${slave}</div>`
      + rows.map(([k, v, c]) => `<div class="r"><span class="k">${k}</span><span class="v ${c}">${v}</span></div>`).join('');

    // viewBox 坐标 → 容器像素坐标（viewBox 有 x/y 偏移，不能只按宽度算）
    const svgRect = this.svg.getBoundingClientRect();
    const wrapRect = el.parentElement.getBoundingClientRect();
    const sx = svgRect.width / this.VB.w;
    const x = (svgRect.left - wrapRect.left) + (cx - this.VB.x) * sx;
    const y = (svgRect.top - wrapRect.top) + (cy - this.VB.y) * sx;

    el.hidden = false;
    const w = el.offsetWidth, h = el.offsetHeight;

    let left = x - w / 2;
    left = Math.max(8, Math.min(left, wrapRect.width - w - 8));

    // 默认贴在节点**图形**上方；放不下翻到下方。
    // 用固定偏移会算出负数被 clamp 到顶边，结果盖住它要描述的那个关节（踩过）。
    const NODE_TOP = 62, NODE_BOTTOM = 96, GAP = 14;
    let top = y - NODE_TOP - h - GAP;
    if (top < 8) top = y + NODE_BOTTOM + GAP;
    top = Math.max(8, Math.min(top, wrapRect.height - h - 8));

    el.style.left = left + 'px';
    el.style.top = top + 'px';
  },

  /* ================= 状态刷新 ================= */
  refresh(st, info) {
    if (!this.svg) return;
    this._st = st;

    const setCls = (linkId, cls) => {
      const l = this._getLink(linkId);
      if (!l) return;
      l.g.setAttribute('class', 'tp-cable ' + cls);
      l.packets.forEach((p) => p.el.setAttribute('class', 'tp-packet tp-packet-' + linkId + ' ' + cls));
    };

    setCls('ws', info.connected ? (info.owner === 'remote' ? 'is-active' : 'is-ok') : 'is-off');

    const ecatCls = !info.connected ? 'is-off'
                  : !info.deviceConnected ? 'is-off'
                  : info.simulated ? 'is-sim' : 'is-ok';
    setCls('ecat', ecatCls);

    // 负载链：RS485 → 模拟输出 → 各控制器。写失败时整条算断，
    // **不能显示"正常"** —— 外设实际没动却显示成功会把排查引到完全错误的方向。
    const loadFailed = !!(st.load && st.load.state === 'failed');
    const loadCls = (info.deviceConnected && !loadFailed) ? 'is-ok' : 'is-off';
    setCls('rs485', loadCls);
    this._joints.forEach((j, i) => {
      setCls('cas' + (i + 1), ecatCls);
      setCls('ch' + i, loadCls);
      setCls('ctrl' + i, loadCls);
      setCls('mech' + (i + 1), 'is-mech');
    });

    // 关节节点：外环颜色 = 该从站自己的驱动状态
    this._joints.forEach((j) => {
      const t = j.slave != null ? st.telemetry.get(j.slave) : null;
      let ring = 'tp-j-ring', sub = '', subCls = 'tp-sub';
      if (j.slave == null) { ring += ' is-off'; sub = '--'; }
      else if (!t || !t.connected) { ring += ' is-off'; sub = '离线'; }
      else {
        const opEnabled = t.driveState === 4;
        const fault = !!t.errorCode || t.driveState === 7;
        ring += fault ? ' is-fault' : opEnabled ? ' is-on' : ' is-idle';
        sub = (DRIVE_STATES[t.driveState] ?? '--') + (t.limitExceeded ? ' · 越限!' : '');
        if (fault) subCls += ' tp-err';
      }
      j.ring.setAttribute('class', ring);
      j.sub.textContent = sub;
      j.sub.setAttribute('class', subCls);
    });

    if (this.pcSub)  this.pcSub.textContent  = `${wsHost}:${WS_PORT}`;
    if (this.armSub) {
      this.armSub.textContent = info.connected
        ? (st.bus || '总线') + ' · ' + info.slaveCount + ' 从站'
        : '未连接';
    }
    if (this.aoSub) {
      // 协议目前只带一路负载 —— 如实说明，不假装三路都受控
      this.aoSub.textContent = st.load && st.load.state
        ? `第 1 路 ${st.load.presetNm} N·m`
        : 'RS485 · Modbus';
    }

    if (this._hoverSlave != null) {
      const j = this._joints.find((x) => x.slave === this._hoverSlave);
      if (j) this._renderDetail(this._hoverSlave, j.cx, this.ROW.joint);
      else this._hideDetail();
    }
  },

  /* ================= 动画 ================= */
  setActive(on) {
    this._active = on;
    if (on && !this._raf) { this._t0 = performance.now(); this._loop(); }
    if (!on && this._raf) { cancelAnimationFrame(this._raf); this._raf = null; }
  },

  _loop() {
    this._raf = requestAnimationFrame(() => this._loop());
    const t = (performance.now() - this._t0) / 1000;
    const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    this._links.forEach((l) => {
      if (!l.flow) return;
      l.packets.forEach((p) => {
        const u = reduce ? p.phase : ((t * 0.2 + p.phase) % 1);
        const pt = l.path.getPointAtLength(u * l.len);
        p.el.setAttribute('cx', pt.x.toFixed(1));
        p.el.setAttribute('cy', pt.y.toFixed(1));
      });
    });
  },
};

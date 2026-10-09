// 系统拓扑与信号走向（组态页的核心）。
//
// 拓扑按 docs/host-embedded-architecture.md 画，**不编**：
//
//   PC 上位机（纯客户端）══WebSocket══► ARM 板（唯一接总线）══EtherCAT══► 关节级联
//                                            │
//                                            └──RS485→Modbus→0-10V──► 磁粉制动器 ──机械耦合──► 关节
//
// 三条设计取舍：
//  1) **从左到右**：信号主干横向流动，负载链挂在 ARM 下方。横向是"流程"的默认读法，
//     比自上而下少一次视线折返。
//  2) **节点用图形不用文字**：显示器认得出是 PC、排针芯片认得出是开发板 ——
//     图先说话，名称只是补充。文字标签放在图形下方，字号压小。
//  3) **连线做成线缆**：深色护套 + 细的信号芯 + 两端接头。一眼能看出这是"一条线"，
//     不是抽象的箭头。
//
// 表现层只做"显示"，不做任何安全判断。

const Topology = {
  svg: null, wrap: null,
  _links: [], _joints: [],
  _active: false, _t0: 0, _raf: null,
  _hoverSlave: null,

  // ---------- 几何（viewBox 1400×620） ----------
  VB: { w: 1400, h: 620 },
  PC:    { cx: 132, cy: 250 },
  ARM:   { cx: 476, cy: 250 },
  BRAKE: { cx: 476, cy: 516 },
  JOINT: { cy: 250, x0: 862, gap: 186 },

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

    // 悬停参数浮层用 HTML 而不是 SVG：文字排版、边框、阴影直接复用现有卡片语汇。
    // 由这里创建而不是写在 index.html 里 —— mount 会清空容器。
    const detail = document.createElement('div');
    detail.className = 'tp-detail';
    detail.hidden = true;
    container.appendChild(detail);

    const svg = this._el('svg', {
      viewBox: `0 0 ${this.VB.w} ${this.VB.h}`, class: 'topo-svg',
    });
    this.svg = svg;

    // 图层：线缆 → 数据包 → 接头 → 节点。包在节点之下，穿过时不盖住文字。
    this.gCables  = this._el('g', { class: 'topo-cables' });
    this.gPackets = this._el('g', { class: 'topo-packets' });
    this.gNodes   = this._el('g', { class: 'topo-nodes' });
    svg.append(this.gCables, this.gPackets, this.gNodes);
    container.appendChild(svg);

    this._buildStatic();
    this._buildJoints([]);
  },

  /* ================= 静态节点：PC / ARM / 制动器 ================= */

  _buildStatic() {
    const P = this.PC, A = this.ARM, B = this.BRAKE;

    // ---- PC 上位机：一台显示器 + 主机 ----
    const pc = this._el('g', { class: 'tp-node tp-node-pc', 'data-node': 'pc' });
    pc.append(
      // 主机（立在显示器左边）
      this._el('rect', { x: P.cx - 66, y: P.cy - 26, width: 26, height: 58, rx: 4, class: 'tp-metal' }),
      this._el('rect', { x: P.cx - 61, y: P.cy - 18, width: 16, height: 3, rx: 1.5, class: 'tp-slot' }),
      this._el('rect', { x: P.cx - 61, y: P.cy - 11, width: 16, height: 3, rx: 1.5, class: 'tp-slot' }),
      this._el('circle', { cx: P.cx - 53, cy: P.cy + 22, r: 2.4, class: 'tp-led' }),
      // 显示器
      this._el('rect', { x: P.cx - 30, y: P.cy - 40, width: 84, height: 58, rx: 5, class: 'tp-screen-bezel' }),
      this._el('rect', { x: P.cx - 26, y: P.cy - 36, width: 76, height: 50, rx: 3, class: 'tp-screen' }),
      this._el('rect', { x: P.cx - 0, y: P.cy + 18, width: 24, height: 8, class: 'tp-metal' }),   // 支架
      this._el('rect', { x: P.cx - 14, y: P.cy + 26, width: 52, height: 5, rx: 2.5, class: 'tp-metal' }),
    );
    // 屏幕上一条"遥测波形"，暗示这台机器在显示数据
    const wave = [];
    for (let i = 0; i <= 20; i++) {
      wave.push(`${P.cx - 24 + i * 3.6} ${P.cy - 20 + Math.sin(i * 0.8) * 7}`);
    }
    pc.appendChild(this._el('polyline', { points: wave.join(' '), class: 'tp-screen-wave' }));
    this.gNodes.appendChild(pc);
    this._label(P.cx - 6, P.cy + 58, 'PC 上位机', 'tp-name');
    this._label(P.cx - 6, P.cy + 76, '', 'tp-sub', 'pcSub');
    this.pcSub = this.svg.querySelector('[data-sub="pcSub"]');

    // ---- ARM 板：PCB + 芯片 + 排针 ----
    const arm = this._el('g', { class: 'tp-node tp-node-arm', 'data-node': 'arm' });
    arm.append(
      this._el('rect', { x: A.cx - 78, y: A.cy - 52, width: 156, height: 104, rx: 8, class: 'tp-pcb' }),
      // 主控芯片
      this._el('rect', { x: A.cx - 54, y: A.cy - 24, width: 46, height: 42, rx: 3, class: 'tp-chip' }),
      this._el('rect', { x: A.cx - 48, y: A.cy - 18, width: 34, height: 30, rx: 2, class: 'tp-chip-die' }),
      // 两颗小芯片
      this._el('rect', { x: A.cx + 4, y: A.cy - 30, width: 30, height: 20, rx: 2, class: 'tp-chip' }),
      this._el('rect', { x: A.cx + 4, y: A.cy - 4, width: 30, height: 20, rx: 2, class: 'tp-chip' }),
      this._el('rect', { x: A.cx + 40, y: A.cy - 30, width: 22, height: 46, rx: 2, class: 'tp-chip' }),
      // 网口 / 接线端子
      this._el('rect', { x: A.cx - 74, y: A.cy + 30, width: 26, height: 16, rx: 2, class: 'tp-jack' }),
      this._el('rect', { x: A.cx + 44, y: A.cy + 30, width: 26, height: 16, rx: 2, class: 'tp-jack' }),
    );
    // 上下两排排针
    for (let i = 0; i < 14; i++) {
      const x = A.cx - 66 + i * 10;
      arm.append(
        this._el('rect', { x, y: A.cy - 60, width: 3, height: 9, rx: 1, class: 'tp-pin' }),
        this._el('rect', { x, y: A.cy + 51, width: 3, height: 9, rx: 1, class: 'tp-pin' }),
      );
    }
    this.gNodes.appendChild(arm);
    this._label(A.cx, A.cy + 78, 'ARM 板 · 下位机', 'tp-name');
    this._label(A.cx, A.cy + 96, '', 'tp-sub', 'armSub');
    this.armSub = this.svg.querySelector('[data-sub="armSub"]');

    // ---- 磁粉制动器：制动盘 + 钳体 ----
    const br = this._el('g', { class: 'tp-node tp-node-brake', 'data-node': 'brake' });
    br.append(
      this._el('circle', { cx: B.cx - 34, cy: B.cy, r: 34, class: 'tp-disc' }),
      this._el('circle', { cx: B.cx - 34, cy: B.cy, r: 22, class: 'tp-disc-inner' }),
      this._el('circle', { cx: B.cx - 34, cy: B.cy, r: 7,  class: 'tp-disc-hub' }),
      this._el('rect', { x: B.cx - 4, y: B.cy - 30, width: 34, height: 60, rx: 5, class: 'tp-caliper' }),
      this._el('rect', { x: B.cx + 30, y: B.cy - 16, width: 22, height: 32, rx: 4, class: 'tp-caliper-body' }),
    );
    // 散热槽
    for (let i = -2; i <= 2; i++) {
      br.append(this._el('rect', { x: B.cx - 4, y: B.cy + i * 10 - 1.5, width: 34, height: 3, rx: 1.5, class: 'tp-caliper-vent' }));
    }
    this.gNodes.appendChild(br);
    this._label(B.cx, B.cy + 60, '磁粉制动器', 'tp-name');
    this._label(B.cx, B.cy + 78, '', 'tp-sub', 'brakeSub');
    this.brakeSub = this.svg.querySelector('[data-sub="brakeSub"]');

    // ---- 静态线缆 ----
    // WebSocket：PC → ARM
    this._cable('ws', `M${P.cx + 56} ${P.cy} L${A.cx - 84} ${A.cy}`,
      { label: 'WebSocket', sub: '遥测 30~60Hz ↓ · 命令 ↑ · 心跳 200ms' });
    // RS485：ARM ↓ 制动器（走 ARM 正下方，竖向）
    this._cable('rs485', `M${A.cx} ${A.cy + 62} L${B.cx} ${B.cy - 42}`,
      { label: 'RS485 → Modbus → 0-10V', sub: '与 EtherCAT 独立 · 只受控制权约束', side: 'right' });
  },

  _label(x, y, text, cls, subKey) {
    const t = this._el('text', { x, y, class: cls, 'text-anchor': 'middle' },
      subKey ? '' : text);
    if (subKey) t.setAttribute('data-sub', subKey);
    this.gNodes.appendChild(t);
    return t;
  },

  /* ================= 关节（动态） ================= */

  _buildJoints(slaves) {
    this._joints.forEach((j) => j.g.remove());
    this._joints = [];
    this._clearLinksByPrefix('cas');
    this._clearLink('ecat');
    this._clearLink('mech');

    const J = this.JOINT;
    const list = slaves.length ? slaves : [null];

    // 关节多的时候压缩间距，保证整条链还在画布内
    const maxSpan = this.VB.w - 130 - J.x0;
    const step = list.length > 1
      ? Math.min(J.gap, maxSpan / (list.length - 1))
      : J.gap;

    const firstX = J.x0;
    this._cable('ecat', `M${this.ARM.cx + 84} ${J.cy} L${firstX - 62} ${J.cy}`,
      { label: 'EtherCAT', sub: '主站 ↔ 从站 · 可级联' });

    list.forEach((s, i) => {
      const cx = J.x0 + i * step;
      const g = this._el('g', { class: 'tp-node tp-joint' });

      // 谐波减速器意象：刚轮（外环+齿）→ 柔轮（青）→ 波发生器（琥珀椭圆）
      g.append(
        this._el('circle', { cx, cy: J.cy, r: 44, class: 'tp-j-ring' }),
        this._el('circle', { cx, cy: J.cy, r: 39, class: 'tp-j-teeth' }),
        this._el('g', { class: 'tp-j-spin-flex', style: `transform-origin:${cx}px ${J.cy}px` })
          .appendChild(this._el('circle', { cx, cy: J.cy, r: 31, class: 'tp-j-flex' })),
        this._el('g', { class: 'tp-j-spin-wave', style: `transform-origin:${cx}px ${J.cy}px` })
          .appendChild(this._el('ellipse', { cx, cy: J.cy, rx: 19, ry: 13, class: 'tp-j-wave' })),
        this._el('circle', { cx, cy: J.cy, r: 4, class: 'tp-j-hub' }),
      );

      const name = this._el('text', { x: cx, y: J.cy + 68, class: 'tp-name', 'text-anchor': 'middle' },
        s ? `关节 ${s.slave}` : '无关节');
      const sub = this._el('text', { x: cx, y: J.cy + 86, class: 'tp-sub', 'text-anchor': 'middle' },
        s ? (s.shortName || '--') : '--');
      g.append(name, sub);

      // 悬停目标：整块（含文字）都可触发
      const hit = this._el('rect', {
        x: cx - 58, y: J.cy - 58, width: 116, height: 156, class: 'tp-hit', rx: 10,
      });
      g.insertBefore(hit, g.firstChild);
      if (s) {
        g.style.cursor = 'pointer';
        g.addEventListener('mouseenter', () => this._showDetail(s.slave, cx, J.cy));
        g.addEventListener('mouseleave', () => this._hideDetail());
      }

      this.gNodes.appendChild(g);
      this._joints.push({ g, slave: s ? s.slave : null, cx,
                          ring: g.querySelector('.tp-j-ring'),
                          sub });

      if (i > 0) {
        const px = J.x0 + (i - 1) * step + 62;
        this._cable('cas' + i, `M${px} ${J.cy} L${cx - 62} ${J.cy}`, { label: '', sub: '' });
      }
    });

    // 机械耦合：制动器 → 第一个关节（被测轴）。走下方再折上来，避开主干。
    const b = this.BRAKE;
    this._cable('mech', `M${b.cx + 56} ${b.cy} L${b.cx + 150} ${b.cy} L${b.cx + 150} ${J.cy + 110} L${firstX} ${J.cy + 110} L${firstX} ${J.cy + 52}`,
      { label: '机械耦合', sub: '咬住被测轴', dashed: true, flow: false, side: 'right' });
  },

  setSlaves(slaves) {
    if (!this.svg) return;
    this._hideDetail();
    this._buildJoints(slaves || []);
  },

  /* ================= 线缆 =================
     两层描边：外层深色护套（粗）+ 内层信号芯（细）。默认无芯，通上电才亮。
     两端各加一小段"接头"，让线看起来是插上去的而不是飘着的。 */
  _cable(id, d, opts) {
    const g = this._el('g', { class: 'tp-cable', 'data-cable': id });
    const sheath = this._el('path', { d, class: 'tp-sheath' + (opts.dashed ? ' tp-dash' : '') });
    const core   = this._el('path', { d, class: 'tp-core' });
    g.append(sheath, core);

    // 接头：路径首尾各一小段加粗短块
    try {
      const len = sheath.getTotalLength();
      [0, 1].forEach((end) => {
        const p = sheath.getPointAtLength(end ? len : 0);
        const q = sheath.getPointAtLength(end ? Math.min(len, 14) : Math.min(len, 14));
        const ang = Math.atan2(q.y - p.y, q.x - p.x) * 180 / Math.PI;
        g.appendChild(this._el('rect', {
          x: p.x - 5, y: p.y - 5, width: 10, height: 10, rx: 2,
          transform: `rotate(${ang} ${p.x} ${p.y})`, class: 'tp-plug',
        }));
      });
    } catch (e) { /* getTotalLength 在极短路径上可能抛，忽略 */ }

    if (opts.label) {
      const mid = sheath.getPointAtLength(sheath.getTotalLength() * 0.5);
      const anchor = opts.side === 'left' ? 'end' : 'start';
      const dx = opts.side === 'left' ? -14 : 14;
      g.appendChild(this._el('text', { x: mid.x + dx, y: mid.y - 6, class: 'tp-label', 'text-anchor': anchor }, opts.label));
      if (opts.sub) {
        g.appendChild(this._el('text', { x: mid.x + dx, y: mid.y + 12, class: 'tp-sublabel', 'text-anchor': anchor }, opts.sub));
      }
    }

    this.gCables.appendChild(g);

    const len = sheath.getTotalLength();
    const entry = { id, g, path: sheath, len, flow: opts.flow !== false, packets: [] };
    if (entry.flow) {
      const n = id === 'ws' ? 4 : id === 'rs485' ? 2 : id.startsWith('cas') ? 2 : 5;
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
    const el = this.wrap && this.wrap.querySelector('.tp-detail');
    if (!el) return;
    this._renderDetail(slave, cx, cy);
  },

  _hideDetail() {
    this._hoverSlave = null;
    const el = this.wrap && this.wrap.querySelector('.tp-detail');
    if (el) el.hidden = true;
  },

  /** 浮层锚在节点正上方；靠边时往内收，避免被卡片裁掉 */
  _renderDetail(slave, cx, cy) {
    const el = this.wrap.querySelector('.tp-detail');
    if (!el) return;
    const s = (this._st ? this._st.slaves : []).find((x) => x.slave === slave);
    const t = this._st && this._st.telemetry.get(slave);
    const fault = !!(t && (t.errorCode || t.driveState === 7));
    const hex = (v) => '0x' + v.toString(16).padStart(4, '0');

    const rows = [
      ['从站号',   `#${slave}`, ''],
      ['型号',     (s && s.model) || '--', ''],
      ['简称',     (s && s.shortName) || '--', ''],
      ['额定力矩', t && t.ratedTorqueNm ? t.ratedTorqueNm.toFixed(1) + ' N·m' : '--', ''],
      ['驱动状态', t ? (DRIVE_STATES[t.driveState] ?? '--') : '--', fault ? 'dg' : ''],
      ['状态字',   t ? hex(t.statusWord) : '--', ''],
      ['故障码',   t ? (t.errorCode ? hex(t.errorCode) : '无') : '--', fault ? 'dg' : ''],
      ['温度',     t && t.temperatureC > 0 ? t.temperatureC.toFixed(1) + ' ℃' : 'N/A', ''],
      ['限位状态', t ? (t.limitExceeded ? '越限' : '正常') : '--', t && t.limitExceeded ? 'dg' : ''],
    ];
    el.innerHTML = `<div class="hd">关节 ${slave}</div>`
      + rows.map(([k, v, c]) => `<div class="r"><span class="k">${k}</span><span class="v ${c}">${v}</span></div>`).join('');

    // SVG 坐标 → 容器像素坐标
    const svgRect = this.svg.getBoundingClientRect();
    const wrapRect = el.parentElement.getBoundingClientRect();
    const sx = svgRect.width / this.VB.w;
    const x = (svgRect.left - wrapRect.left) + cx * sx;
    const y = (svgRect.top - wrapRect.top) + cy * sx;

    el.hidden = false;
    const w = el.offsetWidth, h = el.offsetHeight;

    // 左右：居中锚在节点上，越界就贴边
    let left = x - w / 2;
    left = Math.max(8, Math.min(left, wrapRect.width - w - 8));

    // 上下：默认贴在节点**图形**上方（图形半径约 58，不是整个命中区），
    // 上面放不下就翻到下方 —— 用固定偏移会算出负数，被 clamp 到顶边，
    // 结果浮层正好盖住它要描述的那个关节（踩过）。
    const NODE_TOP = 58, NODE_BOTTOM = 92, GAP = 14;
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

    // WebSocket：本页持有控制权时用青色（"这条线上的命令是我发的"）
    setCls('ws', info.connected ? (info.owner === 'remote' ? 'is-active' : 'is-ok') : 'is-off');

    // EtherCAT 主干与级联段共用一个状态
    const ecatCls = !info.connected ? 'is-off'
                  : !info.deviceConnected ? 'is-off'
                  : info.simulated ? 'is-sim' : 'is-ok';
    setCls('ecat', ecatCls);
    this._joints.forEach((j, i) => setCls('cas' + (i + 1), ecatCls));

    // 负载链：写失败时整条链路算断（不能显示"正常"）
    setCls('rs485', info.deviceConnected && !(st.load && st.load.state === 'failed') ? 'is-ok' : 'is-off');
    setCls('mech', 'is-mech');

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

    // 节点下方的补充信息
    if (this.pcSub)  this.pcSub.textContent  = `${wsHost}:${WS_PORT}`;
    if (this.armSub) {
      this.armSub.textContent = info.connected
        ? (st.bus || '总线') + ' · ' + info.slaveCount + ' 从站'
        : '未连接';
    }
    if (this.brakeSub) {
      this.brakeSub.textContent = st.load && st.load.state
        ? `${st.load.presetNm} N·m`
        : '未预设';
    }

    // 悬停中：让浮层跟着遥测实时更新
    if (this._hoverSlave != null) {
      const j = this._joints.find((x) => x.slave === this._hoverSlave);
      if (j) this._renderDetail(this._hoverSlave, j.cx, this.JOINT.cy);
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

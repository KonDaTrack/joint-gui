// 系统拓扑与信号走向（组态页的核心）。
//
// 拓扑按 docs/host-embedded-architecture.md 画，**不编**：
//
//   PC 上位机（本页，纯客户端）
//        ↕ WebSocket + JSON   遥测 30~60Hz ↓ / 命令 ↑ / 心跳 200ms ↑
//   ARM 板（下位机 · 唯一接总线的那台）
//     ├ EtherCAT 主站 + 实时控制回路
//     ├ 安全逻辑（限位 / 看门狗 / 控制权）  ← 权威在 ARM，PC 只发意图
//     └ QWebSocketServer :9002
//        ├──EtherCAT──► 关节 1 ──级联──► 关节 2 ──► …
//        └──RS485──► modbus_ao ──0-10V──► 磁粉制动器 ──机械耦合──► 关节
//
// 两条链路**互相独立**：负载链不受"当前从站"影响，只受控制权约束。
//
// 表现层只做"显示"，不做任何安全判断。

const Topology = {
  svg: null, root: null,
  _links: [], _joints: [], _packets: [],
  _active: false, _t0: 0, _raf: null,

  NODES: {
    pc:    { cx: 530, cy: 78,  w: 250, h: 62 },
    arm:   { cx: 530, cy: 285, w: 380, h: 128 },
    brake: { cx: 880, cy: 520, w: 190, h: 66 },
  },
  JOINT: { y: 520, w: 140, h: 66, x0: 180, step: 190 },

  NS: 'http://www.w3.org/2000/svg',
  _el(tag, attrs, text) {
    const e = document.createElementNS(this.NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (text != null) e.textContent = text;
    return e;
  },

  mount(container) {
    if (!container) return;
    container.innerHTML = '';
    const svg = this._el('svg', { viewBox: '0 0 1080 640', class: 'topo-svg' });
    this.svg = svg;

    // 图层顺序：连线 → 数据包 → 节点。包在节点下面，穿节点时不会盖住文字。
    this.gLinks = this._el('g', { class: 'topo-links' });
    this.gPackets = this._el('g', { class: 'topo-packets' });
    this.gNodes = this._el('g', { class: 'topo-nodes' });
    svg.append(this.gLinks, this.gPackets, this.gNodes);
    container.appendChild(svg);

    this._buildNodes();
    this._buildJoints([]);
  },

  /* ---------------- 节点 ---------------- */

  _buildNodes() {
    const N = this.NODES;

    // --- PC 上位机 ---
    this.pcBox = this._box(N.pc, 'host');
    const pcL1 = this._el('text', { x: N.pc.cx, y: N.pc.cy - 6, class: 'tp-t1', 'text-anchor': 'middle' }, 'PC 上位机');
    const pcL2 = this._el('text', { x: N.pc.cx, y: N.pc.cy + 14, class: 'tp-t3', 'text-anchor': 'middle' }, '本页 · 纯客户端 · 不碰总线');
    this.gNodes.append(pcL1, pcL2);

    // --- ARM 板 ---
    const arm = this._box(N.arm, 'mcu');
    const aL1 = this._el('text', { x: N.arm.cx - N.arm.w / 2 + 18, y: N.arm.cy - 40, class: 'tp-t1' }, 'ARM 板 · 下位机');
    const aTag = this._el('text', { x: N.arm.cx + N.arm.w / 2 - 18, y: N.arm.cy - 40, class: 'tp-tag', 'text-anchor': 'end' }, '唯一接总线');
    this.gNodes.append(arm, aL1, aTag);

    const rows = [
      ['EtherCAT 主站 + 实时控制回路', 'plain'],
      ['安全逻辑：限位 / 看门狗 / 控制权', 'auth'],
      ['QWebSocketServer :9002', 'plain'],
    ];
    rows.forEach((r, i) => {
      const y = N.arm.cy - 12 + i * 24;
      const x = N.arm.cx - N.arm.w / 2 + 18;
      const bullet = this._el('circle', { cx: x + 3, cy: y - 4, r: 3.2,
        class: r[1] === 'auth' ? 'tp-dot-auth' : 'tp-dot' });
      const tx = this._el('text', { x: x + 16, y, class: r[1] === 'auth' ? 'tp-t2-auth' : 'tp-t2' }, r[0]);
      this.gNodes.append(bullet, tx);
      if (r[1] === 'auth') {
        this.gNodes.append(this._el('text',
          { x: N.arm.cx + N.arm.w / 2 - 18, y, class: 'tp-tag-warn', 'text-anchor': 'end' }, '权威'));
      }
    });

    // --- 磁粉制动器 ---
    this.brakeBox = this._box(N.brake, 'load');
    this.gNodes.append(
      this._el('text', { x: N.brake.cx, y: N.brake.cy - 6, class: 'tp-t1', 'text-anchor': 'middle' }, '磁粉制动器'),
      this._el('text', { x: N.brake.cx, y: N.brake.cy + 14, class: 'tp-t3', 'text-anchor': 'middle' }, '负载 · 0-10V'),
    );

    // --- 静态连线 ---
    this._link('ws', `M${N.pc.cx} ${N.pc.cy + N.pc.h / 2} L${N.pc.cx} ${N.arm.cy - N.arm.h / 2}`,
      { label: 'WebSocket + JSON', sub: '遥测 30~60Hz ↓ · 命令 ↑ · 心跳 200ms', side: 'right' });

    this._link('rs485', `M${N.arm.cx + 130} ${N.arm.cy + N.arm.h / 2} L${N.arm.cx + 130} 430 L${N.brake.cx} 430 L${N.brake.cx} ${N.brake.cy - N.brake.h / 2}`,
      { label: 'RS485 → Modbus → 0-10V', sub: '与 EtherCAT 独立 · 只受控制权约束', side: 'right' });

    // 机械耦合：制动器咬住被测轴（走下方，避开级联线）
    this._link('load', `M${N.brake.cx} ${N.brake.cy + N.brake.h / 2} L${N.brake.cx} 600 L360 600 L360 553`,
      { label: '机械耦合', sub: '', side: 'right', dashed: true, flow: false });
  },

  _box(n, kind) {
    const g = this._el('rect', {
      x: n.cx - n.w / 2, y: n.cy - n.h / 2, width: n.w, height: n.h,
      rx: 14, class: 'tp-box tp-box-' + kind,
    });
    this.gNodes.appendChild(g);
    return g;
  },

  /* ---------------- 关节（动态） ---------------- */

  _buildJoints(slaves) {
    this._joints.forEach((j) => j.g.remove());
    this._joints = [];
    // 上一批的级联线也要清掉，否则重复 setSlaves 会一遍遍叠加
    this._clearLinksByPrefix('cas');

    const J = this.JOINT;
    const list = slaves.length ? slaves : [null];

    // EtherCAT 主干：ARM 左下 → 第一个关节
    this._clearLink('ecat');
    const firstX = J.x0;
    this._link('ecat', `M${this.NODES.arm.cx - 130} ${this.NODES.arm.cy + this.NODES.arm.h / 2} L${this.NODES.arm.cx - 130} 430 L${firstX} 430 L${firstX} ${J.y - J.h / 2}`,
      { label: 'EtherCAT', sub: '主站 ↔ 从站 · 可级联', side: 'left' });

    list.forEach((s, i) => {
      const cx = J.x0 + i * J.step;
      const g = this._el('g', { class: 'tp-joint' });

      const box = this._el('rect', {
        x: cx - J.w / 2, y: J.y - J.h / 2, width: J.w, height: J.h,
        rx: 12, class: 'tp-box tp-box-joint',
      });
      const name = this._el('text', { x: cx, y: J.y - 8, class: 'tp-t1', 'text-anchor': 'middle' },
        s ? `关节 ${s.slave}` : '无关节');
      const model = this._el('text', { x: cx, y: J.y + 10, class: 'tp-t3', 'text-anchor': 'middle' },
        s ? (s.shortName || s.model || '--') : '--');
      const dot = this._el('circle', { cx: cx - J.w / 2 + 16, cy: J.y - 8, r: 3.6, class: 'tp-dot' });
      const stateT = this._el('text', { x: cx, y: J.y + 26, class: 'tp-t3', 'text-anchor': 'middle' }, '');

      g.append(box, dot, name, model, stateT);
      this.gNodes.appendChild(g);
      this._joints.push({ g, box, dot, stateT, slave: s ? s.slave : null, cx });

      // 级联线 + 关节到干线的接入线
      if (i === 0) {
        // 干线已在 _link('ecat') 里画到第一个关节
      } else {
        const px = J.x0 + (i - 1) * J.step + J.w / 2;
        this._link('cas' + i, `M${px} ${J.y} L${cx - J.w / 2} ${J.y}`,
          { label: '', sub: '', side: 'right' });
      }
    });

  },

  /** 从站列表变化时重建关节节点。它由 hello/connection 触发，频率远低于遥测，
   *  重建成本可接受；而每个节点的**状态**更新走 refresh()，不重建。 */
  setSlaves(slaves) {
    if (!this.svg) return;
    this._buildJoints(slaves || []);
  },

  /* ---------------- 连线 ---------------- */

  _link(id, d, opts) {
    const g = this._el('g', { class: 'tp-link', 'data-link': id });
    const path = this._el('path', { d, class: 'tp-path' + (opts.dashed ? ' tp-path-dash' : '') });
    g.appendChild(path);

    // 标签：贴在路径中段旁边
    if (opts.label) {
      const mid = path.getPointAtLength(path.getTotalLength() * 0.5);
      const anchor = opts.side === 'left' ? 'end' : 'start';
      const dx = opts.side === 'left' ? -12 : 12;
      const l1 = this._el('text', { x: mid.x + dx, y: mid.y - 4, class: 'tp-label', 'text-anchor': anchor }, opts.label);
      g.appendChild(l1);
      if (opts.sub) {
        g.appendChild(this._el('text', { x: mid.x + dx, y: mid.y + 12, class: 'tp-sublabel', 'text-anchor': anchor }, opts.sub));
      }
    }

    this.gLinks.appendChild(g);

    const len = path.getTotalLength();
    const entry = { id, g, path, len, flow: opts.flow !== false, packets: [] };

    // 数据包：每条线上若干个等相位分布的小点
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

  /** 按前缀清连线。重建关节时必须把上一批 cas* 清掉，否则会一遍遍叠加。 */
  _clearLinksByPrefix(prefix) {
    this._links.filter((l) => l.id.startsWith(prefix))
      .map((l) => l.id)
      .forEach((id) => this._clearLink(id));
  },

  _getLink(id) { return this._links.find((l) => l.id === id); },

  /* ---------------- 状态刷新 ---------------- */

  /**
   * @param {object} st    app.js 的 state
   * @param {object} info  { connected, deviceConnected, simulated, owner }
   */
  refresh(st, info) {
    if (!this.svg) return;
    const setCls = (linkId, cls) => {
      const l = this._getLink(linkId);
      if (l) l.g.setAttribute('class', 'tp-link ' + cls);
      if (l) l.packets.forEach((p) => p.el.setAttribute('class', 'tp-packet tp-packet-' + linkId + ' ' + cls));
    };

    // WebSocket 链路
    setCls('ws', info.connected ? (info.owner === 'remote' ? 'is-active' : 'is-ok') : 'is-off');

    // EtherCAT 链路
    const ecatCls = !info.connected ? 'is-off'
                  : !info.deviceConnected ? 'is-off'
                  : info.simulated ? 'is-sim' : 'is-ok';
    setCls('ecat', ecatCls);
    this._joints.forEach((j, i) => { if (i > 0) setCls('cas' + (i + 1), ecatCls); });

    // 负载链路
    setCls('rs485', info.deviceConnected && st.load && st.load.state !== 'failed' ? 'is-ok' : 'is-off');
    setCls('load', 'is-mech');

    // 关节节点：每个从站自己的驱动状态
    this._joints.forEach((j) => {
      const t = j.slave != null ? st.telemetry.get(j.slave) : null;
      let cls = 'tp-dot', text = '';
      if (!t || !t.connected) { cls = 'tp-dot'; text = '离线'; }
      else {
        const opEnabled = t.driveState === 4;
        const fault = !!t.errorCode || t.driveState === 7;
        cls = fault ? 'tp-dot-fault' : opEnabled ? 'tp-dot-ok' : 'tp-dot-warn';
        text = (DRIVE_STATES[t.driveState] ?? '--') + (t.limitExceeded ? ' · 越限!' : '');
      }
      j.dot.setAttribute('class', cls);
      j.stateT.textContent = text;
      j.stateT.setAttribute('class', 'tp-t3' + (t && (t.errorCode || t.driveState === 7) ? ' tp-err' : ''));
      j.box.setAttribute('class', 'tp-box tp-box-joint'
        + (t && t.connected && t.driveState === 4 ? ' is-on' : ''));
    });
  },

  /* ---------------- 动画 ---------------- */

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
        // 减少动效时：不动，摆在固定位置（线的颜色仍然表达状态）
        const u = reduce ? p.phase : ((t * 0.22 + p.phase) % 1);
        const pt = l.path.getPointAtLength(u * l.len);
        p.el.setAttribute('cx', pt.x.toFixed(1));
        p.el.setAttribute('cy', pt.y.toFixed(1));
      });
    });
  },
};

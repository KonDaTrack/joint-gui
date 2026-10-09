// 应用组装：状态 + 渲染 + 事件。界面表现层，不含任何安全判断
// （安全逻辑一律在下位机 Qt 端强制，这里恒权只是让按钮灰掉）。

// 与 Joint::DriveState 枚举顺序一一对应（src/device/JointTypes.h）
const DRIVE_STATES = ['未就绪', '禁止合闸', '待合闸', '已合闸', '运行使能',
                      '快速停机', '故障反应', '故障', '未知'];
const OP_ENABLED = 4;   // OperationEnabled

// 下位机（Qt 端 ControlServer）的地址解析。**顺序很重要**：
//
//   1) ?host=192.168.x.x   —— 显式指定，最高优先（发链接给别人时用）
//   2) 上次用过的地址       —— 存在 localStorage，填一次就记住
//   3) location.hostname   —— **仅在它不是回环地址时**才采用
//   4) DEFAULT_HOST        —— 兜底
//
// 第 3 条的"仅非回环"限定是**必须的**，这是实际踩过的坑：
// 上位机在工控机上跑，用 http://localhost:8080 打开页面时 location.hostname 就是
// 'localhost'，于是它去连本机的 9002 —— 而下位机在 ARM 板上，永远连不上。
// 更糟的是界面看不出"它到底在连哪"，表现成"板子没起来"，排查方向完全错。
//
// 第 2 条是这次新增的：换网段/换板子时填一次即可，不用每次都在地址栏挂参数。
const DEFAULT_HOST = '192.168.1.10';   // ARM 板（LubanCat）的出厂网段，可在初始页改
const WS_PORT = 9002;                  // 与 src/ui/MainWindow.cpp 的 kRemotePort 保持一致
const HOST_KEY = 'joint.wsHost';       // localStorage 键

const isLoopback = (h) =>
  !h || h === 'localhost' || h === '127.0.0.1' || h === '::1' || h === '0.0.0.0';

function resolveHost() {
  const q = new URLSearchParams(location.search).get('host');
  if (q) return { host: q, from: '地址栏参数' };
  let saved = null;
  try { saved = localStorage.getItem(HOST_KEY); } catch (e) { /* 无痕模式等，忽略 */ }
  if (saved) return { host: saved, from: '上次使用' };
  if (!isLoopback(location.hostname)) return { host: location.hostname, from: '当前主机' };
  return { host: DEFAULT_HOST, from: '默认值' };
}

const _hostInfo = resolveHost();
const wsHost = _hostInfo.host;
const wsHostFrom = _hostInfo.from;
const link = new JointLink(`ws://${wsHost}:${WS_PORT}`);
const chart = new Chart(document.getElementById('chart'));

const $ = (id) => document.getElementById(id);
const state = {
  bus: '', simulated: false, owner: 'local',
  deviceConnected: false,   // 下位机是否已连上设备（区别于"已连上下位机"）
  slaves: [], active: 0, telemetry: new Map(), lastTelemetry: null,
  rate: { n: 0, t0: 0, hz: 0 },
  // 用于识别"变化"以触发动画（动画只该由变化触发，而不是每次刷新）
  prevDriveState: null, prevHadError: false,
  gfx: Gfx.caps,   // 图形能力（gfx.js 在加载时已探测一次）
  page: 'config',  // 当前页面，由 Router 维护（开场画面不在路由里）
  // 负载链状态：组态页的拓扑图要用它判断 RS485 链路是否正常
  load: { state: '', presetNm: 0 },
  // 下位机自报的从站数量。-1 = 还没收到。与本页列表长度对不上 = 列表过期
  reportedCount: -1,
};

// ============ 提示条 ============
let toastTimer = null;
function toast(msg) {
  const el = $('toast');
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.hidden = true; }, 4000);
}

// ============ 运动进行中（跨页提醒，**不是锁**） ============
// 下发目标后置位，用来在安全条上提示"关节正在动"，任何页面都看得见。
//
// 为什么不做硬锁：堵转时关节速度恒为 0、永远不"完成"，锁死会把人困在监控页
// 出不去（负载文档明确写了堵转不设超时）。提醒保留操作员的选择权。
//
// 为什么不复用 chart.recording：那个在切从站时会被 chart.stop() 清掉，
// 会把"还在动"的指示误清成"停了"。这里独立维护。
const MOTION = {
  active: false, moved: false, stillSince: 0, t0: 0,
  MAX_MS: 60000,   // 硬超时兜底：堵转/判据失效时不至于一直挂着"运动中"
  STILL: 0.5,      // deg/s，与 chart.js 收尾判据同值
  HOLD: 500,       // ms，同上
};

function motionStart() {
  MOTION.active = true; MOTION.moved = false;
  MOTION.stillSince = 0; MOTION.t0 = performance.now();
  renderMotionBadge();
}

/** 立即清掉。停稳、中止、故障、断连、切从站都走这里（一律 fail-open） */
function motionClear() {
  if (!MOTION.active) return;
  MOTION.active = false; MOTION.moved = false; MOTION.stillSince = 0;
  renderMotionBadge();
}

/** 每次遥测调一次：先"动过"再连续静止 HOLD 才算结束 */
function motionTick(t) {
  if (!MOTION.active || !t) return;
  const now = performance.now();
  if (now - MOTION.t0 > MOTION.MAX_MS) return motionClear();
  if (Math.abs(t.velocityDps) >= MOTION.STILL) { MOTION.moved = true; MOTION.stillSince = 0; }
  else if (MOTION.moved) {
    if (!MOTION.stillSince) MOTION.stillSince = now;
    else if (now - MOTION.stillSince > MOTION.HOLD) motionClear();
  }
}

function renderMotionBadge() {
  const el = $('motionBadge');
  if (el) el.hidden = !MOTION.active;
}

// ============ 渲染 ============
// 安全条：跨页常驻，三个页面共用同一个 DOM 实例。
// 它替代了原来监控页顶栏右半部分（总线/控制权/请求按钮）——那段本就该是全局的。
// 身份（关节型号）也从监控页的 .module-badge 挪到了这里，让读数区腾出高度。
function renderSafetyBar() {
  // 四种状态必须区分开：只连上下位机 ≠ 下位机连上了设备。
  // 混为一谈会让人以为关节已经连好（尤其仿真时数据看着和真机一样）。
  const badge = $('busBadge');
  if (!link.connected) {
    badge.className = 'badge badge-idle';
    badge.textContent = '未连接下位机';
  } else if (!state.deviceConnected) {
    badge.className = 'badge badge-idle';
    badge.textContent = '已连下位机 · 设备未连接';
  } else if (state.simulated) {
    badge.className = 'badge badge-sim';
    badge.textContent = '⚠️ 仿真数据（非真机）';
  } else {
    badge.className = 'badge badge-real';
    badge.textContent = `${state.bus || '总线'} 已连接`;
  }

  const remote = state.owner === 'remote';
  $('ownerBadge').className = 'badge ' + (remote ? 'badge-remote' : 'badge-local');
  $('ownerBadge').textContent = remote ? '控制权：上位机（本页）' : '控制权：本机（下位机）';
  $('btnTakeControl').hidden = remote;
  $('btnReleaseControl').hidden = !remote;
  $('btnTakeControl').disabled = !link.connected;
}

/** 无控制权或未确认安全时，命令类按钮置灰；急停/失能/停止始终可用 */
function renderCommandEnabled() {
  // 注意：owner 的含义在下位机侧与本页**相反**——
  //   下位机侧 "remote" = 别的端持有 → 本地禁用
  //   本页     "remote" = 本页自己持有 → 应当启用
  // 照抄下位机的判断会把本页反锁（踩过）。
  const mine = state.owner === 'remote';
  const ready = $('chkReady').checked;
  $('btnEnable').disabled = !(mine && ready);
  $('btnFaultReset').disabled = !mine;
  $('btnSend').disabled = !mine;
  $('selMode').disabled = !mine;
  ['inpPos', 'inpVel', 'inpTor', 'inpProfVel', 'inpProfAcc', 'inpProfDec']
    .forEach((id) => { $(id).disabled = !mine; });
  // 归零/回0 与负载同属"命令类"，无控制权时置灰
  ['btnHome', 'btnZero', 'loadSlider', 'loadValue', 'btnLoadSet', 'btnLoadRelease']
    .forEach((id) => { $(id).disabled = !mine; });
  // 没有控制权时说明原因，避免"点了没反应"
  $('cmdHint').textContent = mine
    ? (ready ? '' : '请先勾选「已确认现场安全」')
    : '当前控制权在下位机（本机）—— 请先点右上角「请求控制权」';
}

/**
 * 应用一份从站列表。hello（握手）与 slaves（推送）**都走这里**，
 * 保证两条路径的行为完全一致——分开写迟早会走偏。
 * @param {Array} list           [{slave, shortName, model, active}]
 * @param {number|null} activeSlave  下位机指定的当前从站；null 表示沿用列表里的 active 标记
 */
function applySlaves(list, activeSlave) {
  state.slaves = list || [];
  state.slaveCount = state.slaves.length;

  const want = Number.isFinite(activeSlave) ? activeSlave : null;
  const act = want != null ? state.slaves.find((s) => s.slave === want)
                           : state.slaves.find((s) => s.active);
  const prev = state.active;
  state.active = act ? act.slave : (state.slaves[0] ? state.slaves[0].slave : 0);

  // 当前观察轴真的变了：波形和"运动中"指示都不再成立
  if (state.active !== prev) { chart.stop(); motionClear(); }

  renderSlaves();
  Topology.setSlaves(state.slaves);
  renderCommandEnabled();
  renderSplash();
  renderConfigLive();
}

function renderSlaves() {
  const ul = $('slaveList');
  ul.innerHTML = '';
  if (state.slaves.length === 0) {
    const li = document.createElement('li');
    li.className = 'sub';
    li.textContent = '无关节';
    ul.appendChild(li);
    return;
  }
  // 关节标识卡：当前控制的是哪个关节、型号、额定力矩（一眼可见，不用去翻别处）
  const cur = state.slaves.find((s) => s.slave === state.active);
  if (cur) {
    $('moduleTitle').textContent = `关节${cur.slave} · ${cur.shortName || '未知型号'}`;
    $('moduleModel').textContent = cur.model || '--';
  } else {
    $('moduleTitle').textContent = '关节 --';
    $('moduleModel').textContent = '--';
  }

  state.slaves.forEach((s) => {
    const li = document.createElement('li');
    li.className = s.slave === state.active ? 'active' : '';
    li.innerHTML = `<span>关节${s.slave} · ${s.shortName || '未知'}</span>
                    <span class="sub">${s.model || ''}</span>`;
    li.onclick = () => {
      if (state.active === s.slave) return;
      state.active = s.slave;
      chart.stop();
      motionClear();   // 换了观察对象，"当前轴在动"的指示不再成立
      // 换轴后重新识别"变化"基线，避免把另一轴的旧状态误判成新变化
      state.prevDriveState = null;
      state.prevHadError = false;
      renderSlaves();
      Anim.slaveSwitch();
      link.command('selectSlave', { slave: s.slave });
    };
    ul.appendChild(li);
  });
}

function renderTelemetry() {
  const t = state.telemetry.get(state.active);
  if (!t) return;
  $('valPos').textContent = t.positionDeg.toFixed(2);
  $('valVel').textContent = t.velocityDps.toFixed(2);
  $('valTor').textContent = t.torqueNm.toFixed(3);

  const st = DRIVE_STATES[t.driveState] ?? '未知';
  // 状态等级只算一次，监控页的圆点和安全条的徽章共用，避免两处判断走偏
  const lvl = !t.connected ? 'offline'
            : t.driveState === OP_ENABLED ? 'ok'
            : (t.errorCode || t.driveState === 7) ? 'fault' : 'warn';

  $('valState').innerHTML =
    `<span class="dot ${lvl}"></span><span>${st}</span>`;

  $('valStatusWord').textContent = '0x' + t.statusWord.toString(16).padStart(4, '0');
  $('valError').textContent = t.errorCode ? '0x' + t.errorCode.toString(16).padStart(4, '0') : '无';
  $('valTemp').textContent = t.temperatureC > 0 ? t.temperatureC.toFixed(1) + ' ℃' : 'N/A';
  $('valRate').textContent = state.rate.hz.toFixed(1) + ' Hz';

  // 安全条上的驱动状态与刷新率。**必须在这里一起更新**——安全条跨页可见，
  // 在组态页/初始页时监控页是 hidden，只更新监控页里的元素等于没更新。
  const db = $('driveBadge');
  if (db) {
    db.className = 'badge ' + (lvl === 'ok' ? 'badge-real'
                             : lvl === 'fault' ? 'badge-fault'
                             : lvl === 'warn' ? 'badge-sim' : 'badge-idle');
    db.textContent = t.connected ? st : '离线';
  }
  const rb = $('rateBadge');
  if (rb) rb.textContent = state.rate.hz.toFixed(1) + ' Hz';

  // 动效只由「变化」触发，不随每次刷新播放——否则 50Hz 下会一直闪
  if (state.prevDriveState !== t.driveState) {
    if (t.driveState === OP_ENABLED) Anim.driveEnabled();
    state.prevDriveState = t.driveState;
  }
  const hadError = !!t.errorCode;
  if (hadError && !state.prevHadError) Anim.faultAppeared();
  state.prevHadError = hadError;
}

// ============ 开场画面（启动时一次，连上即进场） ============
// ★ 它**不是**"未连接状态"的显示：中途断连绝不重新弹出，否则会盖住急停与
//   整个操作界面 —— 那是安全事故，不是体验问题。只在加载时显示，之后永久移除。
//
// 三个时间常量是有意为之：连得快时不能让动画一闪而过；
// 连不上时又绝不能把人永久挡在开场画面外。
const SPLASH_MIN_MS  = 1400;   // 最短显示时长，保证开场动画看得完
const SPLASH_FORM_MS = 4000;   // 连不上这么久，才滑出地址表单与"直接进入"

// t0 必须在**脚本求值那一刻**就取好。设成 0 的话，首次 renderSplash() 算出的
// "已过时间"是个巨大值，会立刻越过 SPLASH_FORM_MS 把地址表单弹出来（踩过）。
const SPLASH = { t0: performance.now(), gone: false, formShown: false, timer: null };

function splashStep(name, cls) {
  const li = document.querySelector(`#splashSteps li[data-step="${name}"]`);
  if (li) li.className = cls;
}

function renderSplash() {
  if (SPLASH.gone) return;

  const linked  = link.connected;
  const helloed = !!state.bus;
  const dev     = !!state.deviceConnected;

  // 三步进度：完成打勾 / 进行中呼吸 / 未开始暗着
  splashStep('link',   linked ? 'done' : 'doing');
  splashStep('hello',  helloed ? 'done' : (linked ? 'doing' : ''));
  splashStep('device', dev ? 'done' : (helloed ? 'doing' : ''));

  const dot = $('bootDot');
  // 带上地址**来源**：地址配错时，"它到底在连哪、这个值哪来的"必须一眼看出
  const target = `ws://${wsHost}:${WS_PORT}（${wsHostFrom}）`;
  let cls = 'dot', text = '';
  if (!linked) {
    // ★ 不能只说"未连接"——加载时就在自动连了，只说"未连接"会让人以为要手动点
    cls = 'dot warn bounce'; text = `正在连接 ${target} …`;
  } else if (!dev) {
    cls = 'dot warn'; text = `已连下位机，设备未连接 · ${target}`;
  } else if (state.simulated) {
    cls = 'dot warn'; text = `⚠️ 仿真数据（非真机）· ${target}`;
  } else {
    cls = 'dot ok'; text = `${state.bus || '总线'} 已连接 · ${target}`;
  }
  dot.className = cls;
  $('bootStatus').textContent = text;

  const inp = $('inpHost');
  if (inp && document.activeElement !== inp) inp.value = wsHost;

  // 表单只在"没连上 **且** 等够久"时出现；连上就收回。
  // 用纯状态驱动而不是"置位后不再清"——否则慢连的情况下表单会一直留着，
  // 而连上之后还摆着地址框纯属噪音（踩过）。
  const elapsed = performance.now() - SPLASH.t0;
  const showForm = !linked && elapsed > SPLASH_FORM_MS;
  if (showForm !== SPLASH.formShown) {
    SPLASH.formShown = showForm;
    $('splashForm').hidden = !showForm;
    $('splashHint').hidden = !showForm;
  }

  renderEnterButton(elapsed);
}

/**
 * 「进入」按钮的状态。**只负责何时露出，不自动进场** ——
 * 按用户要求：连上后由人点击才进正式界面，给一个"我准备好了"的确认动作。
 *
 * 但**必须保证按钮最终一定会出现**：连不上就永远进不去的话，
 * 那比自动进场还糟。所以连不上到 SPLASH_FORM_MS 时，把它降级成次要的
 * "不连接，直接进入"——进得去，只是不显眼。
 */
function renderEnterButton(elapsed) {
  const btn = $('btnEnter');
  if (!btn) return;

  const connected = link.connected;
  const ready = elapsed >= SPLASH_MIN_MS && (connected || elapsed >= SPLASH_FORM_MS);
  if (!ready) { btn.hidden = true; return; }

  const secondary = !connected;
  btn.hidden = false;
  btn.classList.toggle('is-secondary', secondary);
  const txt = secondary ? '不连接，直接进入' : '进入';
  if (btn.textContent !== txt) btn.textContent = txt;
}

function leaveSplash() {
  if (SPLASH.gone) return;
  SPLASH.gone = true;
  stopSplashTimer();

  const el = $('splash');
  el.classList.add('leaving');
  let fin = false;
  const done = () => {
    if (fin) return;
    fin = true;
    el.classList.add('gone');                 // 过渡结束才真摘掉：直接 hidden 会跳过动画
    document.body.classList.add('app-in');    // 主界面入场
  };
  el.addEventListener('transitionend', done, { once: true });
  setTimeout(done, 900);                      // 兜底：过渡事件没来也要摘掉

  Router.go('config');                        // 落到正式界面
}

/** 手动叫回连接界面（换 IP / 换板子时用）。不会自动关闭。 */
function reopenSplash() {
  SPLASH.gone = false;
  SPLASH.formShown = true;
  const el = $('splash');
  el.classList.remove('gone', 'leaving');
  document.body.classList.remove('app-in');
  $('splashForm').hidden = false;
  $('splashHint').hidden = false;
  startSplashTimer();
  renderSplash();
}

function startSplashTimer() {
  if (SPLASH.timer) return;
  SPLASH.timer = setInterval(() => {
    if (SPLASH.gone) return stopSplashTimer();
    renderSplash();   // 内部会更新「进入」按钮的状态
  }, 120);
}
function stopSplashTimer() {
  if (SPLASH.timer) { clearInterval(SPLASH.timer); SPLASH.timer = null; }
}

// ============ 组态页 ============
const fmtHex = (v) => '0x' + v.toString(16).padStart(4, '0');

function renderParams() {
  const s = state.slaves.find((x) => x.slave === state.active);
  const t = state.telemetry.get(state.active);
  const fault = !!(t && (t.errorCode || t.driveState === 7));

  // [标签, 值, 值样式类]
  const rows = [
    ['从站号',   s ? `#${s.slave}` : '--', ''],
    ['型号',     s ? (s.model || '--') : '--', ''],
    ['简称',     s ? (s.shortName || '--') : '--', ''],
    ['总线',     state.bus ? state.bus + (state.simulated ? '（仿真）' : '') : '--',
                 state.simulated ? 'wn' : ''],
    ['额定力矩', t && t.ratedTorqueNm ? t.ratedTorqueNm.toFixed(0) + ' N·m' : '--', ''],
    ['驱动状态', t ? (DRIVE_STATES[t.driveState] ?? '--') : '--', fault ? 'dg' : ''],
    ['状态字',   t ? fmtHex(t.statusWord) : '--', ''],
    ['故障码',   t ? (t.errorCode ? fmtHex(t.errorCode) : '无') : '--', fault ? 'dg' : ''],
    ['温度',     t && t.temperatureC > 0 ? t.temperatureC.toFixed(1) + ' ℃' : 'N/A', ''],
    ['限位状态', t ? (t.limitExceeded ? '越限' : '正常') : '--', t && t.limitExceeded ? 'dg' : ''],
  ];

  $('paramList').innerHTML = rows.map(([k, v, cls]) =>
    `<div class="p-row"><div class="k">${k}</div><div class="v ${cls}">${v}</div></div>`).join('');

  $('paramNote').textContent =
    '参数只读：全部由下位机上报。安全逻辑（行程限位、看门狗、控制权）在 ARM 侧强制执行，'
    + '本页只显示，不做任何判断。';
}

/** 拓扑图与参数只在组态页可见时刷新 —— 50Hz 下没必要给隐藏页面做 DOM 写入 */
function renderConfigLive() {
  if (state.page !== 'config') return;
  renderParams();
  Topology.refresh(state, {
    connected: link.connected,
    deviceConnected: state.deviceConnected,
    simulated: state.simulated,
    owner: state.owner,
  });
  const n = $('topoNote');
  if (n) {
    n.textContent = !link.connected ? '未连接下位机'
                  : state.simulated ? '⚠️ 仿真链路（非真机）'
                  : '链路实时状态';
  }

  // 从站列表过期告警：下位机自报的数量与手上的列表对不上。
  // 新版下位机会主动推 slaves 消息，这条主要是**对接老版本时的兜底**——
  // 否则操作员只会看到"无关节/少了一个"，完全不知道是列表没同步。
  // 放在这里而不是开场画面：开场画面连上就消失，告警放那儿等于看不见。
  const warn = $('bootWarn');
  if (warn) {
    const stale = state.reportedCount >= 0 && state.reportedCount !== state.slaves.length;
    warn.hidden = !stale;
    if (stale) {
      warn.textContent =
        `下位机报告 ${state.reportedCount} 个从站，本页只有 ${state.slaves.length} 个 —— `
        + `点顶栏左侧的关节名重新连接，会重新握手取回完整列表。`;
    }
  }
}

// ============ 事件 ============
link.on('open', () => { renderSafetyBar(); renderSplash(); toast('已连接下位机'); })
    .on('close', () => {
      renderSafetyBar(); chart.stop(); motionClear(); renderSplash();
      // 开场画面还没退场时不弹提示：那时很可能**从来没连上过**，
      // 说"连接已断开"是错的；而且开场画面自己的状态行已经在说"正在连接…"了
      if (SPLASH.gone) toast('连接已断开，正在重连…');
    })
    .on('hello', (m) => {
      state.bus = m.bus || '';
      state.simulated = !!m.simulated;
      state.deviceConnected = !!m.bus;   // 总线名为空 = 下位机还没连上任何设备
      state.owner = m.owner || 'local';
      // 恢复负载状态：不复位的话重连后滑块归 0，而制动器可能正咬着上一个值，
      // 操作者会拿一个错的值去下发目标。
      if (m.load) {
        if (Number.isFinite(m.load.presetNm)) renderLoad(m.load.presetNm);
        loadHint(m.load.note || '', m.load.state !== 'failed');
      }
      applySlaves(m.slaves, null);
      renderSafetyBar();
    })
    // 从站列表的**推送**。hello 只在握手时发一次，而下位机扫到从站往往晚于
    // 客户端连上——没有这条消息的话，网页会一直停在"无关节"，只能靠刷新。
    .on('slaves', (m) => {
      const before = state.slaves.length;
      applySlaves(m.slaves, m.activeSlave);
      renderSafetyBar();
      if (state.slaves.length !== before) {
        toast(`从站列表已更新：${state.slaves.length} 个`);
      }
    })
    .on('telemetry', (m) => {
      state.owner = m.owner || state.owner;
      (m.slaves || []).forEach((t) => state.telemetry.set(t.slave, t));

      // 刷新率统计
      const r = state.rate;
      if (!r.t0) r.t0 = performance.now();
      if (++r.n >= 20) {
        r.hz = (r.n * 1000) / (performance.now() - r.t0);
        r.n = 0; r.t0 = performance.now();
      }

      renderTelemetry();
      const t = state.telemetry.get(state.active);
      if (t && t.connected) chart.push(t, m.ts);

      // 运动指示：故障/掉线立即清掉（fail-open），否则按"先动过再停稳"判结束
      if (t) {
        if (!t.connected || t.errorCode || t.driveState === 7) motionClear();
        else motionTick(t);
      }

      renderConfigLive();   // 内部判页，非组态页直接返回
    })
    .on('controlOwner', (m) => {
      const changed = state.owner !== m.owner;
      state.owner = m.owner;
      renderSafetyBar(); renderCommandEnabled();
      // 只在真正变化时脉冲（renderTopbar 会被多处调用，不能都触发动画）
      if (changed) Anim.ownershipChanged();
      toast(m.owner === 'remote' ? `控制权已接管：${m.reason}` : `控制权在本机：${m.reason}`);
    })
    .on('fault', (m) => toast('下位机异常：' + m.message))
    .on('loadState', (m) => {
      // 下位机如实回报。**绝不乐观显示"已生效"**——写失败时制动器毫无反应，
      // 而界面显示成功会把排查引到完全错误的方向（这个项目吃过同类亏）。
      // torqueNm 为 -1 表示"外设实际状态未知"，与"0"含义不同。
      const applied = Number.isFinite(m.torqueNm) ? m.torqueNm : 0;
      const volt    = Number.isFinite(m.volt) ? m.volt : 0;
      const preset  = Number.isFinite(m.presetNm) ? m.presetNm : 0;
      state.load = { state: m.state, presetNm: preset };   // 拓扑图用
      if (m.state === 'preset' || m.state === 'cleared') renderLoad(preset);

      const text = {
        preset:  `已预设 ${preset} N·m（${(preset / NM_PER_VOLT).toFixed(2)} V），点「下发目标」时施加`,
        writing: m.note || '正在写入负载…',
        applied: `负载已生效：${applied} N·m（${volt.toFixed(2)} V）`,
        cleared: '负载已清零',
        failed:  `负载失败：${m.note || '未知原因'}`,
      }[m.state] || (m.note || '');
      loadHint(text, m.state !== 'failed');
    })
    .on('connection', (m) => {
      state.deviceConnected = !!m.connected;
      state.bus = m.bus || '';   // 断开时下位机传空串，不能沿用旧值否则仍显示"已连接"
      state.simulated = (state.bus === '仿真');
      // 下位机报的从站数量。与本页手里的列表对不上 = 列表过期。
      // 新版本下位机会在扫到从站时推 slaves 消息；这条是**对接老版本时的兜底提示**。
      const n = Number(m.slaveCount);
      state.reportedCount = Number.isFinite(n) ? n : state.slaves.length;
      renderSafetyBar();
      renderSplash();
      renderConfigLive();
      toast(m.connected ? `下位机已连接设备（${state.bus}）` : '下位机未连接设备');
    });

$('btnTakeControl').onclick = () => link.requestControl();
$('btnReleaseControl').onclick = () => link.releaseControl();
$('chkReady').onchange = renderCommandEnabled;

// 安全条上的急停与失能。**不受控制权限制，也不受当前页面限制**——
// 这是设计文档 4.4 的硬约束，任何时候都要能点。
// 与监控页操作卡里的同名按钮是两个入口、同一个命令，不冲突。
$('btnEstopBar').onclick = () => { motionClear(); link.command('estop'); };
$('btnDisableBar').onclick = () => { motionClear(); link.command('disable'); };

// 「运动中」徽章可点，点了跳回监控页（它是提醒，不做任何拦截）
$('motionBadge').onclick = () => Router.go('monitor');

$('btnEnable').onclick = () => { link.command('setMode', { mode: +$('selMode').value });
                                 link.command('enable'); };
$('btnDisable').onclick = () => { motionClear(); link.command('disable'); };
$('btnFaultReset').onclick = () => link.command('faultReset');
// 归零（标定零点）与回0（走回零点）——下位机已实现，这里补上入口与 Qt 端对齐
$('btnHome').onclick = () => link.command('homing');
$('btnZero').onclick = () => link.command('moveToZero');
$('btnEstop').onclick = () => { motionClear(); link.command('estop'); };  // 不受控制权限制
// 「停止运动」用独立命令名，不走 setTarget：那条路会先写一次负载，
// 而"停下来"绝不该顺手施加负载（停止时反而必须撤载）。
$('btnStop').onclick = () => { motionClear(); link.command('stopMotion'); };

$('btnSend').onclick = () => {
  const mode = +$('selMode').value;
  const args = {};
  if (mode === 1) {   // PP：位置 + 轮廓
    args.positionDeg = +$('inpPos').value;
    args.profileVelocity = +$('inpProfVel').value;
    args.profileAcceleration = +$('inpProfAcc').value;
    args.profileDeceleration = +$('inpProfDec').value;
  } else if (mode === 3) {  // PV：速度 + 轮廓加减速
    args.velocityDps = +$('inpVel').value;
    args.profileAcceleration = +$('inpProfAcc').value;
    args.profileDeceleration = +$('inpProfDec').value;
  } else {            // PT：力矩 + 力矩斜率
    args.torqueNm = +$('inpTor').value;
    args.torqueSlope = +$('inpTorSlope').value;   // 与 Qt 端同名字段对齐，不再写死
  }
  // 负载值**随目标一起发**（同一条消息）：分两条会有竞态，而且下位机要保证
  // "先写负载、确认成功、再发运动指令"的顺序。0 时省略 = 本次不带负载。
  if (loadPresetNm > 0) args.loadNm = loadPresetNm;
  link.command('setTarget', args);
  // 不要乐观提示"已生效"——写负载可能要几百毫秒，失败还会拦住这次运动
  loadHint(loadPresetNm > 0 ? '下发中…（先写负载，确认后再发运动指令）' : '', true);
  const t = state.telemetry.get(state.active);
  motionStart();                          // 安全条上亮起「运动中」
  chart.start(t ? t.ratedTorqueNm : 0);   // 每次下发都重新记录本次响应
  Anim.chartStarted();
};

// 模式切换时只显示相关字段
// 按模式显示相关字段。初始状态不播动画——否则会和入场动画叠在一起
function applyModeFields(animate) {
  const mode = +$('selMode').value;
  const want = mode === 1 ? 'pos' : mode === 3 ? 'vel' : 'tor';
  document.querySelectorAll('.field[data-mode]').forEach((f) => {
    f.classList.toggle('hidden', !f.dataset.mode.split(' ').includes(want));
  });
  if (animate) Anim.fieldsChanged();
}
$('selMode').onchange = () => applyModeFields(true);
applyModeFields(false);

// ============ 负载控制（磁粉制动器 / 张力控制器） ============
// 走 RS485 → Modbus → 0-10V，与关节的 EtherCAT 是**两条独立链路**，
// 所以它不受"控制从站"影响，只受控制权约束。
// 标定：制动器的 50 N·m ↔ 10V（AO 模块满量程）。模块单位 mV，所以 mV = N·m × 200。
// 电压换算只用于**显示**——下位机一律用 torqueNm 自己算 mV，不信客户端传的值。
const LOAD_RATED_NM = 50;    // 额定：只显示，不硬性限制输入
const NM_PER_VOLT   = 5;     // 50 / 10
let loadPresetNm = 0;        // 预设值：只记住，点「下发目标」时才施加

const loadHint = (msg, ok) => {
  $('loadHint').textContent = msg || '';
  $('loadHint').style.color = ok ? 'var(--ok)' : 'var(--warn)';
};

/** 按 N·m 刷新滑块/数字框/电压显示。超额定只标红提示，不夹取 */
function renderLoad(nm) {
  // NaN 必须挡住：JSON.stringify(NaN) → null → Qt 的 toDouble() → 0，
  // 负载会**静默变成 0**（制动器松开）——危险方向的静默失败。
  if (!Number.isFinite(nm)) nm = 0;
  nm = Math.max(0, nm);

  const s = Math.min(nm, LOAD_RATED_NM);      // 滑块行程 = 额定（要超额定只能改数字框）
  $('loadSlider').value = s;
  $('loadSlider').style.setProperty('--fill', (s / LOAD_RATED_NM * 100) + '%');

  if (document.activeElement !== $('loadValue')) $('loadValue').value = nm;

  const over = nm > LOAD_RATED_NM;
  $('loadVolt').textContent = (nm / NM_PER_VOLT).toFixed(2) + ' V' + (over ? '（超额定）' : '');
  loadPresetNm = nm;
}

$('loadSlider').oninput = () => { renderLoad(+$('loadSlider').value); loadHint(''); };
$('loadValue').oninput  = () => { renderLoad(+$('loadValue').value);  loadHint(''); };

$('btnLoadSet').onclick = () => {
  renderLoad(+$('loadValue').value);
  // 只记住：真正施加发生在点「下发目标」时（先写负载、确认成功再发运动指令）
  link.command('setLoad', { torqueNm: loadPresetNm });
  loadHint(`已预设 ${loadPresetNm} N·m（${(loadPresetNm / NM_PER_VOLT).toFixed(2)} V），`
         + `点「下发目标」时施加`, true);
};

$('btnLoadRelease').onclick = () => {
  link.command('releaseLoad', {});
  loadHint('正在松开负载…', true);
};

renderLoad(0);

// ============ 读数字号自适应 ============
// 「位置/速度/力矩」是操作员盯得最多的三块，**任何窗口尺寸下都不该出现滚动条**。
// 固定字号做不到：实测 1440×900 溢出 46px、1366×768 差 44px。
// 而 CSS 里算不出可用高度（要减掉型号条、遥测表、内外边距，还随边框盒变），
// 所以这里**实测反推**：二分几次，逼出"刚好不溢出"的最大字号。
// 只在尺寸变化时跑，不跟渲染帧走。
// 下限 14px：1366×768 这类矮窗口要压到 ~15px 才装得下，
// 卡在 16 会差 5px → 又冒出滚动条。14px 仍清晰可读，再往下就不划算了。
const READOUT_MIN = 14, READOUT_MAX = 44;

function fitReadouts() {
  const box = document.querySelector('.readouts');
  if (!box || !box.clientHeight || !box.querySelector('.readout')) return;

  // 返回当前字号下的溢出量。读 scrollHeight 会同步触发布局，不用额外等待。
  const overflowAt = (size) => {
    box.style.setProperty('--readout-size', size.toFixed(1) + 'px');
    return box.scrollHeight - box.clientHeight;
  };

  if (overflowAt(READOUT_MAX) <= 0) return;      // 大屏：直接用上限，省掉二分
  let lo = READOUT_MIN, hi = READOUT_MAX;
  for (let i = 0; i < 7 && hi - lo > 0.5; i++) {
    const mid = (lo + hi) / 2;
    if (overflowAt(mid) <= 0) lo = mid; else hi = mid;
  }
  overflowAt(lo);
}

fitReadouts();
// 首次布局时字体可能还没加载完，行高会变——字体就绪后再量一次
if (document.fonts && document.fonts.ready) document.fonts.ready.then(fitReadouts);
let fitPending = false;
window.addEventListener('resize', () => {
  if (fitPending) return;
  fitPending = true;
  requestAnimationFrame(() => { fitPending = false; fitReadouts(); });
});
// 卡片高度由网格决定，改字号不会反过来改变它 → 不会形成观察者死循环
new ResizeObserver(fitReadouts).observe(document.querySelector('.card-slaves'));

// ============ 页面路由 ============
// hidden 状态下页面内所有元素尺寸为 0，箱内做尺寸自适应的东西会算错。
// 波形 canvas 与读数区各自挂了 ResizeObserver，显示时会自己重算；
// 这里再补一次，免疫 display:none → block 时的观察器时序差异。
// 先注册再 init —— 否则首次落位不会触发回调。
Router.onChange((name, prev) => {
  state.page = name;
  // 带着正在执行的运动切离监控页 → 轻提示一次。
  // **不弹确认框、不拦路**：这是提醒，操作员的选择权保留。
  if (prev === 'monitor' && name !== 'monitor' && MOTION.active) {
    toast('关节运动执行中 —— 点顶部「运动中」可回到监控页');
  }
  if (name === 'monitor') requestAnimationFrame(() => fitReadouts());

  // 拓扑动画只在组态页可见时跑：rAF 循环不该给隐藏页面白烧 CPU
  Topology.setActive(name === 'config');
  if (name === 'config') renderConfigLive();
  renderSplash();
});

// 拓扑图先挂载再 init —— init 会触发 onChange，可能立刻就要 refresh
Topology.mount(document.getElementById('topo'));

// 导航里只剩正式界面。开场画面不是"一页"，连上后被摘掉，不参与路由。
Router.init('config');

// ============ 开场画面的动作 ============
// 换地址只能重载：ws.js 的连接 URL 在加载时就定死了。
// 走 ?host= 这条既有机制，行为与直接用链接打开完全一致，不会有两套路径。
$('btnConnect').onclick = () => {
  const h = $('inpHost').value.trim();
  if (!h) return;
  // 记住它：下次打开默认就用这个地址，不用再填。
  // 无痕模式下 setItem 会抛异常，忽略即可——本次跳转仍然生效。
  try { localStorage.setItem(HOST_KEY, h); } catch (e) { /* 忽略 */ }
  // 必须清掉 ?host=：它的优先级最高，留着会把刚记住的地址盖掉，
  // 表现为"改了地址却没用"——正是这个坑让人以为连接坏了。
  const url = new URL(location.href);
  url.searchParams.delete('host');
  location.href = url.toString();
};
// 「进入」：**点了才进正式界面**。这是有意的确认动作 ——
// 操作员看过连接状态、确认无误后再进入，比被自动推进去更符合试验台的使用习惯。
$('btnEnter').onclick = () => leaveSplash();

// 换 IP / 换板子时的入口。没有它，开场画面消失后就再也回不去了。
$('safetyIdent').onclick = () => reopenSplash();
$('safetyIdent').title = '点击可更换下位机地址';

renderSafetyBar(); renderCommandEnabled(); renderSlaves();
renderSplash();
startSplashTimer();
Anim.entrance();
Anim.bindButtonFeedback();
link.connect();

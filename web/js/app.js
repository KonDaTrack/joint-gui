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
  page: 'monitor', // 当前页面，由 Router 维护（开场画面不在路由里）
  // 负载链状态（协议目前只有一路）。用于负载提示，以及判断写负载是否失败。
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
// 安全条：常驻，所有页面共用同一个 DOM 实例。
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
  renderCommandEnabled();
  renderSplash();
  renderSlaveWarn();
}

/** 切换当前控制的关节。菜单项和（将来的）其它入口都走这里。 */
function selectSlave(id) {
  if (state.active === id) { closeIdentMenu(); return; }
  state.active = id;
  chart.stop();
  motionClear();   // 换了观察对象，"当前轴在动"的指示不再成立
  // 换轴后重新识别"变化"基线，避免把另一轴的旧状态误判成新变化
  state.prevDriveState = null;
  state.prevHadError = false;
  renderSlaves();
  renderCommandEnabled();
  renderTelemetry();
  Anim.slaveSwitch();
  link.command('selectSlave', { slave: id });
  closeIdentMenu();
}

/** 安全条左侧的名字 + 点开后的切换菜单（原「在线关节」那一整行收进来的） */
function renderSlaves() {
  const cur = state.slaves.find((s) => s.slave === state.active);
  $('moduleTitle').textContent = cur
    ? `关节${cur.slave} · ${cur.shortName || '未知型号'}` : '关节 --';
  $('moduleModel').textContent = cur ? (cur.model || '--') : '--';

  const menu = $('identMenu');
  if (!menu) return;
  const items = state.slaves.length
    ? state.slaves.map((s) => `
        <button role="menuitem" data-slave="${s.slave}" class="${s.slave === state.active ? 'on' : ''}">
          <span>关节${s.slave} · ${s.shortName || '未知'}</span>
          <span class="sm">${s.model || ''}</span>
        </button>`).join('')
    : '<div class="h" style="padding:8px 10px">尚未扫到关节</div>';

  menu.innerHTML =
    `<div class="h">切换关节</div>${items}`
    + `<div class="sep"></div>`
    + `<button role="menuitem" data-act="host">更换下位机地址…</button>`;

  menu.querySelectorAll('button[data-slave]').forEach((b) => {
    b.onclick = () => selectSlave(+b.dataset.slave);
  });
  const host = menu.querySelector('button[data-act="host"]');
  if (host) host.onclick = () => { closeIdentMenu(); reopenSplash(); };
}

/* ---- 菜单开合 ---- */
function openIdentMenu() {
  const menu = $('identMenu'), id = $('safetyIdent');
  if (!menu || !id) return;
  menu.hidden = false;
  id.setAttribute('aria-expanded', 'true');
  const r = id.getBoundingClientRect();
  menu.style.left = Math.round(r.left) + 'px';
  menu.style.top = Math.round(r.bottom + 6) + 'px';
  // 越界就往回收，别顶出窗口
  const mw = menu.offsetWidth;
  menu.style.left = Math.round(Math.min(r.left, window.innerWidth - mw - 8)) + 'px';
}
function closeIdentMenu() {
  const menu = $('identMenu'), id = $('safetyIdent');
  if (menu) menu.hidden = true;
  if (id) id.setAttribute('aria-expanded', 'false');
}
function toggleIdentMenu() {
  const menu = $('identMenu');
  if (menu && menu.hidden) openIdentMenu(); else closeIdentMenu();
}

function renderTelemetry() {
  const t = state.telemetry.get(state.active);
  if (!t) return;

  // 位置/速度/力矩走径向仪表（读数框已换成表盘 —— 表盘还告诉你"占了多少量程"）
  Gauges.update(t);
  // 三维关节：只喂数据，渲染由它自己的 rAF 驱动。
  // 它是 ES 模块、deferred 执行，所以这里必须判空 —— app.js 跑在它前面。
  if (window.Joint3D) window.Joint3D.update(t);

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
  $('valRate').textContent =
    (Number.isFinite(state.rate.hz) ? state.rate.hz.toFixed(1) : '--') + ' Hz';

  // 剩余行程：离 ±170° 限位还有多远。行程来自机械限位，不随关节型号变。
  const LIMIT = 170, p = Number.isFinite(t.positionDeg) ? t.positionDeg : 0;
  const fillEl = $('travelFill');
  if (fillEl) {
    fillEl.style.width = Math.min(50, Math.abs(p) / LIMIT * 50) + '%';
    fillEl.classList.toggle('neg', p < 0);
    $('travelNeg').textContent = (LIMIT + Math.min(p, 0)).toFixed(1) + '°';
    $('travelPos').textContent = (LIMIT - Math.max(p, 0)).toFixed(1) + '°';
  }

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
const SPLASH_FORM_MS = 4500;   // 连不上这么久，才滑出地址表单与"直接进入"

// 每一步**至少停留这么久**才往下滚。连接往往在几百毫秒内就全部完成，
// 不强制停留的话三步一闪而过、滚动根本来不及看（用户反馈："效果没体现出来"）。
// 这是**显示节奏**，不是伪造状态：真实状态照样即时生效，只是不让它瞬间跳到最后。
const STEP_DWELL_MS = 800;
const STEP_NAMES = ['link', 'hello', 'device'];

// t0 必须在**脚本求值那一刻**就取好。设成 0 的话，首次 renderSplash() 算出的
// "已过时间"是个巨大值，会立刻越过 SPLASH_FORM_MS 把地址表单弹出来（踩过）。
const SPLASH = {
  t0: performance.now(), gone: false, formShown: false, timer: null,
  stepIdx: 0, stepAt: performance.now(),
};

/** 真实状态对应第几步（0/1/2），不经过放慢 */
function realStep() {
  if (!link.connected) return 0;
  if (!state.bus) return 1;
  return 2;
}

/** 显示用的步骤号：只做"延迟推进"，绝不跳过 —— 真实状态领先时逐格追上去 */
function displayStep() {
  const now = performance.now();
  if (realStep() > SPLASH.stepIdx && now - SPLASH.stepAt >= STEP_DWELL_MS) {
    SPLASH.stepIdx++;
    SPLASH.stepAt = now;
  }
  return SPLASH.stepIdx;
}

function splashStep(name, cls) {
  const li = document.querySelector(`#splashSteps li[data-step="${name}"]`);
  if (li) li.className = cls;
}

// ★ 行高必须和 shell.css 里 .splash-steps 的 --row 一致，改一处要改两处。
const STEP_ROW = 38;

/** 把第 idx 步滚到取景框正中（像歌词）。缓存上次的值，别每个 tick 都写样式。 */
function splashScroll(idx) {
  const list = $('splashSteps');
  if (!list) return;
  const y = -idx * STEP_ROW;
  if (list._y === y) return;
  list._y = y;
  list.style.transform = `translateY(${y}px)`;
}

function renderSplash() {
  if (SPLASH.gone) return;

  const linked  = link.connected;
  const helloed = !!state.bus;
  const dev     = !!state.deviceConnected;

  // 三步进度：完成打勾 / 进行中呼吸 / 未开始暗着。
  // 用**放慢后**的 idx 驱动显示 —— 否则连接一快，三步瞬间全变 done，滚动看不见。
  const idx = displayStep();
  const allDone = linked && helloed && dev;
  STEP_NAMES.forEach((nm, i) => {
    const cls = i < idx ? 'done'
              : i > idx ? ''
              : (i === STEP_NAMES.length - 1 && allDone ? 'done' : 'doing');
    splashStep(nm, cls);
  });
  splashScroll(idx);

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

  renderSplashSlaves(idx);
  renderEnterButton(elapsed, idx);
}

/** 扫到的从站。**扫完（显示到第 3 步）才出现** —— 开场画面就该让人确认
 *  "到底认出来几个关节"。之前把这块挪到了组态页，结果开场时什么都看不到。 */
function renderSplashSlaves(idx) {
  const box = $('splashSlaves');
  const list = $('splashSlaveList');
  if (!box || !list) return;

  const show = idx >= STEP_NAMES.length - 1 && state.slaves.length > 0;
  box.hidden = !show;
  if (!show) return;

  const html = state.slaves.map((s) => {
    const short = s.shortName || s.model || '未知';
    // 型号串在有些关节上很长（板上会带"· 额定 X N·m"），放进 title 而不是正文
    return `<li title="${(s.model || '').replace(/"/g, '')}">#${s.slave} · ${short}</li>`;
  }).join('');
  if (list._html !== html) { list._html = html; list.innerHTML = html; }
}

/**
 * 「进入」按钮的状态。**只负责何时露出，不自动进场** ——
 * 按用户要求：连上后由人点击才进正式界面，给一个"我准备好了"的确认动作。
 *
 * 但**必须保证按钮最终一定会出现**：连不上就永远进不去的话，
 * 那比自动进场还糟。所以连不上到 SPLASH_FORM_MS 时，把它降级成次要的
 * "不连接，直接进入"——进得去，只是不显眼。
 */
function renderEnterButton(elapsed, idx) {
  const btn = $('btnEnter');
  if (!btn) return;

  const connected = link.connected;
  // 连上之后还要等步骤**滚完**才出主按钮 —— 否则点早了会把开场序列截断，
  // 用户还没看清就进主界面了。
  const stepsSettled = idx >= STEP_NAMES.length - 1;
  const ready = elapsed >= SPLASH_MIN_MS
             && ((connected && stepsSettled) || elapsed >= SPLASH_FORM_MS);
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
  el.classList.add('leaving');   // 光圈放大穿越 + 文字上浮

  // ★ 主界面**在开场画面还没飞完时就开始装配**（260ms 时光圈刚放大到一半）。
  //   两段重叠才有衔接感；等它彻底消失再入场是两次独立动画，中间空一拍。
  //   这个时刻要早于开场画面淡过半 —— 卡片回到起始位的那一瞬间会被盖住。
  setTimeout(() => document.body.classList.add('app-in'), 260);

  // 飞完了再摘掉。用固定时长而不是 transitionend：后者在多个过渡属性并存时
  // 只为最先结束的那个触发一次，时机不确定。
  setTimeout(() => el.classList.add('gone'), 1260);

  Router.go('config');           // 落到正式界面
}

/** 手动叫回连接界面（换 IP / 换板子时用）。不会自动关闭。 */
function reopenSplash() {
  SPLASH.gone = false;
  SPLASH.formShown = true;
  // 已经连上了再叫回来，步骤应当**直接显示为已完成**，
  // 而不是从头重放一遍扫描动画（那会是在骗人）。
  SPLASH.stepIdx = STEP_NAMES.length - 1;
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

// ============ 从站列表过期告警 ============
// 下位机自报的从站数量与本页手上的列表对不上 → 列表没同步。
// 新版下位机会主动推 slaves 消息，这条主要是**对接老版本时的兜底**，
// 否则操作员只会看到"无关节/少了一个"，完全不知道是列表没同步。
// 挂在监控页顶栏那张在线关节列表旁边 —— 它本来就是关于那张列表的。
function renderSlaveWarn() {
  const warn = $('bootWarn');
  if (!warn) return;
  const stale = state.reportedCount >= 0 && state.reportedCount !== state.slaves.length;
  warn.hidden = !stale;
  if (stale) {
    warn.textContent =
      `下位机报告 ${state.reportedCount} 个从站，本页只有 ${state.slaves.length} 个 —— `
      + `点上方关节名重新连接，会重新握手取回完整列表。`;
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

      // 刷新率统计。dt 为 0 时必须跳过 —— 除零会算出 Infinity Hz 显示出来
      // （虚拟时间/时钟抖动下真的会，看着像"刷新率爆表"）。
      const r = state.rate;
      if (!r.t0) r.t0 = performance.now();
      if (++r.n >= 20) {
        const dt = performance.now() - r.t0;
        if (dt > 0) r.hz = (r.n * 1000) / dt;
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

      renderSlaveWarn();
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
      renderSlaveWarn();
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
  // 把目标值一起传进波形，它会画一条目标虚线 —— 一眼看出到没到位。
  // 按模式只传对应的那一路：PV 的目标是速度，画到位置窗格上是错的。
  const targets = {};
  if (mode === 1) targets.positionDeg = args.positionDeg;
  else if (mode === 3) targets.velocityDps = args.velocityDps;
  else targets.torqueNm = args.torqueNm;
  chart.start(t ? t.ratedTorqueNm : 0, targets);   // 每次下发都重新记录本次响应
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

// ============ 径向仪表 ============
// 位置/速度/力矩的数字框已换成表盘，原来那套"二分逼出最大字号"的自适应
// （fitReadouts）随之删除 —— 表盘是 SVG 按容器比例缩放，不存在溢出问题。
Gauges.mount();

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
  // 切到监控页要重测画布尺寸。**必须显式重测**，不能只靠 ResizeObserver：
  // 页面显示时它不一定触发（实测 2/3 概率不触发），波形会一直空白。
  // 三维视图同理 —— 它也是自己的 canvas。
  if (name === 'monitor') {
    requestAnimationFrame(() => {
      if (chart && chart.resize) chart.resize();
      if (window.Joint3D) window.Joint3D.resize();
    });
    setTimeout(() => { if (chart && chart.resize) chart.resize(); }, 700);
  }

  renderSlaveWarn();
  renderSplash();
});

// 导航里只剩正式界面。开场画面不是"一页"，连上后被摘掉，不参与路由。
Router.init('monitor');

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

// 点关节名 → 下拉菜单：切关节 + 换下位机地址。
// 「更换下位机地址」也是开场画面消失后**唯一**能回去的入口（没有它换网段就无路可走）。
$('safetyIdent').onclick = (e) => { e.stopPropagation(); toggleIdentMenu(); };
$('safetyIdent').onkeydown = (e) => {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleIdentMenu(); }
};
document.addEventListener('click', (e) => {
  const id = $('safetyIdent'), menu = $('identMenu');
  if (!id || !menu || menu.hidden) return;
  if (!id.contains(e.target) && !menu.contains(e.target)) closeIdentMenu();
});
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeIdentMenu(); });
window.addEventListener('resize', closeIdentMenu);

// ---- 三维视图全屏 ----
// 全屏的是 .joint-box（不是 canvas），提示文字、图例、按钮都跟着进去。
// 全屏后画布尺寸变了，**必须显式重测** —— joint3d 的 ResizeObserver 会触发，
// 但和 chart 一样存在"显示时不一定触发"的风险，补一次保险。
$('btnJointFull').onclick = () => {
  const box = $('jointBox');
  if (!box) return;
  if (document.fullscreenElement) document.exitFullscreen();
  else if (box.requestFullscreen) box.requestFullscreen();
};
$('btnZoomIn').onclick    = () => window.Joint3D && Joint3D.zoomBy(0.78);
$('btnZoomOut').onclick   = () => window.Joint3D && Joint3D.zoomBy(1 / 0.78);
$('btnZoomReset').onclick = () => window.Joint3D && Joint3D.resetView();

document.addEventListener('fullscreenchange', () => {
  // 全屏是**瞬时**切换的，画布尺寸会瞬间跳变并伴随一次重排。
  // 给容器重放一次短促的淡入把这一跳盖住 —— 否则看起来是"闪一下"。
  const box = $('jointBox');
  if (box) {
    box.classList.remove('joint-smooth');
    void box.offsetWidth;               // 强制重排，否则同一个 class 不会重放动画
    box.classList.add('joint-smooth');
  }
  // 镜头先回拉再归位：把"模型突然变大/变小"变成一个有意做的镜头动作。
  // 光靠淡入盖不住尺寸变化 —— 人能感知到"东西变了大小"。
  if (window.Joint3D) window.Joint3D.settle();
  setTimeout(() => { if (window.Joint3D) window.Joint3D.resize(); }, 80);
});

renderSafetyBar(); renderCommandEnabled(); renderSlaves();
renderSplash();
startSplashTimer();
// 不再调 Anim.entrance()：它在页面加载时就跑，而那时整块主界面被开场画面盖着，
// 动画根本看不见；更糟的是 GSAP 会留下内联 transform，与 body.app-in 那套
// CSS 装配动画打架。入场动画现在统一由 .app-in 负责（见 shell.css）。
Anim.bindButtonFeedback();
link.connect();

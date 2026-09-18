// 应用组装：状态 + 渲染 + 事件。界面表现层，不含任何安全判断
// （安全逻辑一律在下位机 Qt 端强制，这里恒权只是让按钮灰掉）。

// 与 Joint::DriveState 枚举顺序一一对应（src/device/JointTypes.h）
const DRIVE_STATES = ['未就绪', '禁止合闸', '待合闸', '已合闸', '运行使能',
                      '快速停机', '故障反应', '故障', '未知'];
const OP_ENABLED = 4;   // OperationEnabled

// 下位机（Qt 端 ControlServer）的地址。优先级：
//   1) ?host=192.168.x.x  —— 换网段/换板子时不用改代码
//   2) 页面所在主机       —— 在板子上开个 http 服务、用板子的浏览器打开本页时，
//                            location.hostname 就是板子自己，直接连通
//   3) DEFAULT_HOST       —— 双开 index.html 的常规情况
//
// 第 3 条是**必须的**：file:// 打开时 location.hostname 是空串，原来会回退成
// 'localhost'，那在下位机与网页同机时恰好成立；但下位机搬到 ARM 板之后，
// 'localhost' 指的是 PC 自己，永远连不上——而且表现成"板子没起来"，极易误判。
const DEFAULT_HOST = '192.168.1.10';   // ARM 板（LubanCat）的地址
const WS_PORT = 9002;                  // 与 src/ui/MainWindow.cpp 的 kRemotePort 保持一致

const wsHost = new URLSearchParams(location.search).get('host')
            || location.hostname
            || DEFAULT_HOST;
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

// ============ 渲染 ============
function renderTopbar() {
  // 三种状态必须区分开：只连上下位机 ≠ 下位机连上了设备。
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
  $('valState').innerHTML =
    `<span class="dot ${!t.connected ? 'offline'
      : t.driveState === OP_ENABLED ? 'ok'
      : t.errorCode ? 'fault'
      : t.driveState === 7 ? 'fault' : 'warn'}"></span><span>${st}</span>`;

  $('valStatusWord').textContent = '0x' + t.statusWord.toString(16).padStart(4, '0');
  $('valError').textContent = t.errorCode ? '0x' + t.errorCode.toString(16).padStart(4, '0') : '无';
  $('valTemp').textContent = t.temperatureC > 0 ? t.temperatureC.toFixed(1) + ' ℃' : 'N/A';
  $('valRate').textContent = state.rate.hz.toFixed(1) + ' Hz';

  // 动效只由「变化」触发，不随每次刷新播放——否则 50Hz 下会一直闪
  if (state.prevDriveState !== t.driveState) {
    if (t.driveState === OP_ENABLED) Anim.driveEnabled();
    state.prevDriveState = t.driveState;
  }
  const hadError = !!t.errorCode;
  if (hadError && !state.prevHadError) Anim.faultAppeared();
  state.prevHadError = hadError;
}

// ============ 事件 ============
link.on('open', () => { renderTopbar(); toast('已连接下位机'); })
    .on('close', () => { renderTopbar(); chart.stop(); toast('连接已断开，正在重连…'); })
    .on('hello', (m) => {
      state.bus = m.bus || '';
      state.simulated = !!m.simulated;
      state.deviceConnected = !!m.bus;   // 总线名为空 = 下位机还没连上任何设备
      state.owner = m.owner || 'local';
      state.slaves = m.slaves || [];
      const act = state.slaves.find((s) => s.active);
      state.active = act ? act.slave : (state.slaves[0] ? state.slaves[0].slave : 0);
      // 恢复负载状态：不复位的话重连后滑块归 0，而制动器可能正咬着上一个值，
      // 操作者会拿一个错的值去下发目标。
      if (m.load) {
        if (Number.isFinite(m.load.presetNm)) renderLoad(m.load.presetNm);
        loadHint(m.load.note || '', m.load.state !== 'failed');
      }
      renderTopbar(); renderSlaves(); renderCommandEnabled();
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
    })
    .on('controlOwner', (m) => {
      const changed = state.owner !== m.owner;
      state.owner = m.owner;
      renderTopbar(); renderCommandEnabled();
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
      renderTopbar();
      toast(m.connected ? `下位机已连接设备（${state.bus}）` : '下位机未连接设备');
    });

$('btnTakeControl').onclick = () => link.requestControl();
$('btnReleaseControl').onclick = () => link.releaseControl();
$('chkReady').onchange = renderCommandEnabled;

$('btnEnable').onclick = () => { link.command('setMode', { mode: +$('selMode').value });
                                 link.command('enable'); };
$('btnDisable').onclick = () => link.command('disable');
$('btnFaultReset').onclick = () => link.command('faultReset');
// 归零（标定零点）与回0（走回零点）——下位机已实现，这里补上入口与 Qt 端对齐
$('btnHome').onclick = () => link.command('homing');
$('btnZero').onclick = () => link.command('moveToZero');
$('btnEstop').onclick = () => link.command('estop');       // 不受控制权限制
// 「停止运动」用独立命令名，不走 setTarget：那条路会先写一次负载，
// 而"停下来"绝不该顺手施加负载（停止时反而必须撤载）。
$('btnStop').onclick = () => link.command('stopMotion');

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

renderTopbar(); renderCommandEnabled(); renderSlaves();
Anim.entrance();
Anim.bindButtonFeedback();
link.connect();

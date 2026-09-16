// 应用组装：状态 + 渲染 + 事件。界面表现层，不含任何安全判断
// （安全逻辑一律在下位机 Qt 端强制，这里恒权只是让按钮灰掉）。

// 与 Joint::DriveState 枚举顺序一一对应（src/device/JointTypes.h）
const DRIVE_STATES = ['未就绪', '禁止合闸', '待合闸', '已合闸', '运行使能',
                      '快速停机', '故障反应', '故障', '未知'];
const OP_ENABLED = 4;   // OperationEnabled

const link = new JointLink(`ws://${location.hostname || 'localhost'}:9002`);
const chart = new Chart(document.getElementById('chart'));

const $ = (id) => document.getElementById(id);
const state = {
  bus: '', simulated: false, owner: 'local',
  deviceConnected: false,   // 下位机是否已连上设备（区别于"已连上下位机"）
  slaves: [], active: 0, telemetry: new Map(), lastTelemetry: null,
  rate: { n: 0, t0: 0, hz: 0 },
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
  const local = state.owner !== 'remote';
  const ready = $('chkReady').checked;
  $('btnEnable').disabled = !(local && ready);
  $('btnFaultReset').disabled = !local;
  $('btnSend').disabled = !local;
  $('selMode').disabled = !local;
  ['inpPos', 'inpVel', 'inpTor', 'inpProfVel', 'inpProfAcc', 'inpProfDec']
    .forEach((id) => { $(id).disabled = !local; });
}

function renderSlaves() {
  const ul = $('slaveList');
  ul.innerHTML = '';
  if (state.slaves.length === 0) {
    const li = document.createElement('li');
    li.className = 'sub';
    li.textContent = '无从站';
    ul.appendChild(li);
    return;
  }
  state.slaves.forEach((s) => {
    const li = document.createElement('li');
    li.className = s.slave === state.active ? 'active' : '';
    li.innerHTML = `<span>从站${s.slave} · ${s.shortName || '未知'}</span>
                    <span class="sub">${s.model || ''}</span>`;
    li.onclick = () => {
      state.active = s.slave;
      chart.stop();
      renderSlaves();
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
      state.owner = m.owner;
      renderTopbar(); renderCommandEnabled();
      toast(m.owner === 'remote' ? `控制权已接管：${m.reason}` : `控制权在本机：${m.reason}`);
    })
    .on('fault', (m) => toast('下位机异常：' + m.message))
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
$('btnEstop').onclick = () => link.command('estop');       // 不受控制权限制
$('btnStop').onclick = () => link.command('setTarget', { velocityDps: 0 });

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
  } else {            // PT：力矩
    args.torqueNm = +$('inpTor').value;
    args.torqueSlope = 10;
  }
  link.command('setTarget', args);
  const t = state.telemetry.get(state.active);
  chart.start(t ? t.ratedTorqueNm : 0);   // 每次下发都重新记录本次响应
};

// 模式切换时只显示相关字段
$('selMode').onchange = () => {
  const mode = +$('selMode').value;
  const want = mode === 1 ? 'pos' : mode === 3 ? 'vel' : 'tor';
  document.querySelectorAll('.field[data-mode]').forEach((f) => {
    f.classList.toggle('hidden', !f.dataset.mode.split(' ').includes(want));
  });
};
$('selMode').dispatchEvent(new Event('change'));

renderTopbar(); renderCommandEnabled(); renderSlaves();
link.connect();

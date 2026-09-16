#!/usr/bin/env node
// 上位机协议测试客户端（零依赖：用 Node 18+ 的原生 WebSocket）
//
// 用法：
//   node web/test-client.js                      只连上看遥测（只读）
//   node web/test-client.js request              请求控制权，并周期发心跳
//   node web/test-client.js cmd enable           发一条命令后退出
//   node web/test-client.js cmd setTarget '{"positionDeg":30,"profileVelocity":10}'
//   node web/test-client.js cmd estop            急停（不受控制权限制）
//
// 环境变量 JOINT_WS 可改地址，默认 ws://localhost:9002
const url = process.env.JOINT_WS || 'ws://localhost:9002';
const argv = process.argv.slice(2);
const ws = new WebSocket(url);

let heartbeat = null;

const send = (obj) => {
  ws.send(JSON.stringify(obj));
};

ws.addEventListener('open', () => {
  console.log(`已连接 ${url}`);

  if (argv[0] === 'request') {
    send({ type: 'requestControl' });
    heartbeat = setInterval(() => send({ type: 'heartbeat' }), 200);
    console.log('已请求控制权；心跳 200ms。Ctrl+C 退出。');
  } else if (argv[0] === 'cmd') {
    const name = argv[1];
    const args = argv[2] ? JSON.parse(argv[2]) : {};
    send({ type: 'command', name, args });
    console.log(`已发送命令 ${name}`, args);
    setTimeout(() => process.exit(0), 500);
  } else {
    console.log('只读观察模式（未请求控制权）。Ctrl+C 退出。');
  }
});

ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  switch (m.type) {
    case 'hello':
      // 仿真/真机必须一眼可辨（仿真数据看着和真机一样，容易误判）
      console.log(`[hello] 总线=${m.bus}${m.simulated ? ' ⚠️仿真数据' : ''} ` +
                  `控制权=${m.owner} 从站数=${m.slaveCount}`);
      (m.slaves || []).forEach((s) =>
        console.log(`  从站${s.slave} ${s.shortName} ${s.model}${s.active ? ' （当前）' : ''}`));
      break;
    case 'telemetry':
      // 只打第一路，避免刷屏
      if (m.slaves && m.slaves[0]) {
        const t = m.slaves[0];
        console.log(`[遥测] 从站${t.slave} 位置=${t.positionDeg.toFixed(2)}° ` +
                    `速度=${t.velocityDps.toFixed(2)}°/s 力矩=${t.torqueNm.toFixed(3)}N·m ` +
                    `状态字=0x${t.statusWord.toString(16)} 故障=0x${t.errorCode.toString(16)} ` +
                    `| 控制权=${m.owner}`);
      }
      break;
    case 'controlOwner':
      console.log(`[控制权] → ${m.owner}（${m.reason}）`);
      break;
    case 'fault':
      console.log(`[异常] ${m.message}`);
      break;
    case 'connection':
      console.log(`[连接] connected=${m.connected} bus=${m.bus} 从站数=${m.slaveCount}`);
      break;
    default:
      console.log('[未知消息]', m);
  }
});

ws.addEventListener('error', (e) => {
  console.error('连接错误：', e.message || e);
  process.exit(1);
});

ws.addEventListener('close', () => {
  if (heartbeat) clearInterval(heartbeat);
  console.log('连接已关闭');
});

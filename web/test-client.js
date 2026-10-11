#!/usr/bin/env node
// 上位机协议测试客户端（零依赖：用 Node 18+ 的原生 WebSocket）
//
// 用法：
//   node web/test-client.js                      连上看遥测（只读），周期发心跳
//   node web/test-client.js cmd enable           发一条命令后退出
//   node web/test-client.js cmd setTarget '{"positionDeg":30,"profileVelocity":10}'
//   node web/test-client.js cmd estop            急停
//
// 注意：没有 request 模式了 —— 下位机控制权仲裁已删除，连上就是控制端。
// 但**心跳照发**：它是下位机看门狗的输入，超时（1s）会失能所有轴。
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

  // 一直发心跳：不发的话下位机看门狗武装不起来（lastHeartbeatMs_ 恒为 0），
  // 那就等于没有"人不在了自己停"的保护。
  heartbeat = setInterval(() => send({ type: 'heartbeat' }), 200);

  if (argv[0] === 'cmd') {
    const name = argv[1];
    const args = argv[2] ? JSON.parse(argv[2]) : {};
    send({ type: 'command', name, args });
    console.log(`已发送命令 ${name}`, args);
    setTimeout(() => process.exit(0), 500);
  } else {
    console.log('只读观察模式（心跳 200ms）。Ctrl+C 退出。');
  }
});

ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  switch (m.type) {
    case 'hello':
      // 仿真/真机必须一眼可辨（仿真数据看着和真机一样，容易误判）
      console.log(`[hello] 总线=${m.bus}${m.simulated ? ' ⚠️仿真数据' : ''} ` +
                  `从站数=${m.slaveCount}`);
      (m.slaves || []).forEach((s) =>
        console.log(`  从站${s.slave} ${s.shortName} ${s.model}${s.active ? ' （当前）' : ''}`));
      break;
    case 'telemetry':
      // 只打第一路，避免刷屏
      if (m.slaves && m.slaves[0]) {
        const t = m.slaves[0];
        console.log(`[遥测] 从站${t.slave} 位置=${t.positionDeg.toFixed(2)}° ` +
                    `速度=${t.velocityDps.toFixed(2)}°/s 力矩=${t.torqueNm.toFixed(3)}N·m ` +
                    `状态字=0x${t.statusWord.toString(16)} 故障=0x${t.errorCode.toString(16)}`);
      }
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

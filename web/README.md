# Web 上位机

通过 WebSocket 连接下位机（Qt 端 `ControlServer`，默认端口 `9002`）。

架构与安全设计见 [`../docs/host-embedded-architecture.md`](../docs/host-embedded-architecture.md)。

## 启动

**① 先起下位机**（Qt 端，它才是 EtherCAT 主站）：

```bash
cd ..            # 回到 joint-gui
sudo ./run.sh
```

**② 再起 Web 界面**：

```bash
cd web
python3 -m http.server 8080
```

浏览器打开 <http://localhost:8080>

> **为什么不用 `file://` 直接打开**：浏览器对 `file://` 页面发起 WebSocket 有额外限制，
> 各浏览器行为不一致。用一个本地 HTTP 服务最省事（`python3` 自带，无需安装）。

## 功能

| 区域 | 内容 |
|---|---|
| 顶栏 | 连接状态 / **总线类型（仿真会醒目警示）** / 控制权 + 请求·交还按钮 |
| 左栏 | 从站列表（点击切换）、位置/速度/力矩读数、状态字·故障码·温度·刷新率 |
| 中栏 | 急停、安全确认、使能/失能/故障复位、模式与目标下发 |
| 下方 | 实时波形（**点「下发目标」才开始记录**本次响应） |

## 控制权

「两者都能控制」必须仲裁，否则两个界面同时往驱动器写目标，电机行为不可预测。

- **仲裁权在下位机**：本页只能「请求」，不能夺取
- 需先在 Qt 端勾选「允许上位机接管控制」才能请求成功
- 接管/交还时**所有轴都会先失能**（避免接手一个目标未知的运动轴）
- 本页持有控制权时按 200ms 发心跳；**超 1 秒未收到，下位机会自动收回并失能**
- **急停不受控制权限制**，任何时候都能发

## 调试

零依赖的命令行客户端（Node 18+ 原生 WebSocket）：

```bash
node test-client.js                    # 只读观察
node test-client.js request            # 请求控制权并周期发心跳
node test-client.js cmd enable         # 发一条命令
node test-client.js cmd setTarget '{"positionDeg":30,"profileVelocity":10}'
```

## 文件

```
web/
├── index.html
├── css/app.css          配色与 Qt 端一致（同一套色板）
├── js/ws.js             连接层：协议、心跳、重连、控制权
├── js/chart.js          Canvas 实时曲线
├── js/app.js            状态 + 渲染 + 事件
├── lib/gsap.min.js      动画库（GSAP 3.15，本地副本，不走 CDN）
└── test-client.js       命令行测试客户端
```

> GSAP 目前**尚未使用**——动画是后续阶段（P3）的事。先放本地是因为
> 试验台多为离线环境，不能依赖 CDN。

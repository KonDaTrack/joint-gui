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
python3 serve.py
```

浏览器打开 <http://localhost:8080>。**同一局域网里任何一台机器**都可以打开
`http://<这台机器的IP>:8080` —— 上位机侧（Windows / Mac / Linux / 平板）**什么都不用装**，
只要一个现代浏览器：Chrome/Edge 89+、Firefox 108+、Safari 16.4+。

**推荐把 `serve.py` 起在板子上**（而不是某台 x86 机器上），这样地址推导永远正确，
而且终端页开出来的就是板子自己的 shell。见 [`../DEPLOY.md`](../DEPLOY.md) §3.5。

> ★ **不要用 sudo 起 `serve.py`**。终端页里开出来的 shell 就是当前用户的 shell，
> 用 sudo 起等于把 root shell 交出去。Qt 那边要 sudo（EtherCAT 要 raw socket），
> 两个进程分开起。

> **为什么不用 `file://` 直接打开**：三维关节走 ES 模块 + import map，浏览器会拒绝从
> `file://` 加载模块；而且 `file://` 下发 WebSocket 各浏览器行为不一致。用一个本地
> HTTP 服务最省事（`python3` 自带，无需安装）。

> `serve.py` 取代了原来的 `python3 -m http.server 8080`：**同一个端口**上，
> 静态文件照旧，另外多了一条 `/term` 的 WebSocket 给终端页用。
> **纯标准库、零 pip 依赖**（试验台是离线环境，装不了包）。

## 功能

| 区域 | 内容 |
|---|---|
| 顶栏 | 页面导航 / 当前关节 / **总线类型（仿真会醒目警示）** / 运动中提醒 |
| 监控页 | 从站列表（点击切换）、三维关节、位置/速度/力矩读数、遥测、急停与使能、模式与目标下发、实时波形 |
| 历史数据页 | 按「关节 + 模式」筛选历次运行，叠加对比（运行=颜色，轨迹=线型） |
| 终端页 | Terminator 式切分，每格一个**独立 shell**（见下） |

## 终端页

监控、历史之外的第三页。像 Terminator 那样上下左右切分，**每格是一个独立 shell**。

- **切分**：工具栏按钮 / 右键窗格 / `Ctrl`+`Shift`+`O`（左右）`E`（上下）
- **关闭**：`Ctrl`+`Shift`+`W`（至少留一格，最后一格关不掉）
- **最大化**：`Ctrl`+`Shift`+`X`
- 每格一条 WebSocket（`/term`，同源），服务端 fork 一个 PTY

**这些 shell 跑在 `serve.py` 那台机器上**。所以：

- `serve.py` 起在板子上 → 终端就是**板子自己的 shell**（看 `dmesg`、改配置，不用 ssh）
- 起在工控机上 → 终端就是工控机的 shell
- 在那个 shell 里敲 `ssh 用户名@地址`，就是普通的 ssh，随便连

切到别的页 shell **不会死**，回来接着用；关掉窗格或刷新页面才会结束。

> ★ 这条通道就是**机器上的一个 shell，没有鉴权**。别把它暴露到公网。

## 控制权 / 看门狗

**没有"控制权"这回事了。** 本页是唯一的控制端 —— 下位机默认无窗口，
设备侧不再有人操作，也就没有第二个操作者要仲裁。连上就能用。

保留下来的只有**看门狗**，而且它比以前更重要：

- 本页**只要连着就按 200ms 发心跳**
- 下位机超 1 秒收不到心跳，或 WS 断开 → **立即失能所有轴 + 撤载**

因为下位机那边已经没有界面了：网页一掉，就再没有任何人能发出停止命令。
（正常关闭靠 WS 断开触发；网线被拔、页面卡死这种 TCP 半开只能靠心跳兜住，
所以两条都留着。）

- **急停不受任何限制**，任何时候都能发

## 调试

零依赖的命令行客户端（Node 18+ 原生 WebSocket）：

```bash
node test-client.js                    # 只读观察（心跳 200ms）
node test-client.js cmd enable         # 发一条命令
node test-client.js cmd setTarget '{"positionDeg":30,"profileVelocity":10}'
```

## 文件

```
web/
├── index.html
├── serve.py             入口：静态文件 + 终端（同端口）。零 pip 依赖
├── server/              后端（只用 Unix 的 PTY）
│   ├── websocket.py       手写 RFC 6455（标准库没有 WebSocket）
│   ├── pty_base.py        PTY 抽象 + 环境准备
│   ├── pty_unix.py        os.forkpty()
│   └── conn.py            单条连接的生命周期
├── css/                 配色与 Qt 端一致（同一套色板）
├── js/
│   ├── ws.js            连接层：协议、心跳、重连、控制权
│   ├── chart.js         Canvas 实时曲线（三窗格）
│   ├── compare.js       历史对比图
│   ├── history.js       IndexedDB 存储
│   ├── term.js          终端页：二叉树切分 + xterm + WS
│   ├── router.js        页面切换 + hash
│   └── app.js           状态 + 渲染 + 事件
├── lib/
│   ├── gsap.min.js      动画库（本地副本，不走 CDN）
│   ├── three/           三维关节（本地副本）
│   └── xterm/           终端渲染器（本地副本）
├── tests/
│   ├── term_client.js   后端的 Node 对拍脚本（自己起 serve.py）
│   └── ui_term.js       终端页的无头验证（playwright + 系统 Chrome）
└── test-client.js       命令行测试客户端
```

## 测试

```bash
node tests/term_client.js                                   # 后端：回显 / resize / 大帧 / 收尸
NODE_PATH=<playwright 的 node_modules> node tests/ui_term.js  # 前端：切分 / 拖动 / 切页存活
```

> GSAP 目前**尚未使用**——动画是后续阶段（P3）的事。先放本地是因为
> 试验台多为离线环境，不能依赖 CDN。

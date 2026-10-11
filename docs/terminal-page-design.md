# 终端页设计 —— Terminator 式切分

> 2026-10-11 · 状态：待评审

## 0. 由来

监控页、历史数据页之外，再加第三个页面「终端」。要求像 Terminator 一样能上下左右切分。

过程中定下的四件事：

1. **终端跑在上位机**，不是下位机。下位机（Qt / 9002）一行都不改。
2. **每个窗格是一个独立 shell**，切一下多开一个。就是 Terminator 的原义。
3. 窗格里的 shell 用来干什么**不归这个界面管** —— 你可以在里面对任意主机敲 `ssh 用户名@地址`。
4. **Windows 上也要能用**，走 ConPTY。

## 1. 架构

```
浏览器 · 终端页（N 个窗格，每格一个 xterm）
   │   每格一条 WebSocket      ws://<上位机>:9003/term
   ▼
上位机 · serve.py     ← 它取代 python3 -m http.server
   ├─ :8080  静态文件（行为与 http.server 一致）
   └─ :9003  PTY 桥：每条连接 fork/spawn 一个 shell
                Unix    → os.openpty() + fork
                Windows → ctypes 调 ConPTY
```

**最关键的一条：下位机零改动。** 终端整套东西全在上位机这一侧，和 EtherCAT 那条链路完全隔离，不碰 9002、不碰 Qt。

`serve.py` 是**上位机上唯一的常驻进程**，静态服务和终端服务同生共死 —— 启动方式从"两条命令"变成"一条"。

## 2. 后端 `web/serve.py`

**Python 3.12 纯标准库，零 pip 依赖。** 试验台是离线环境，装不了包，所以：

- 静态服务：`http.server.ThreadingHTTPServer`，行为对齐 `python3 -m http.server`
- PTY：自己抽象一层，平台各一条实现（见 2.4）
- WebSocket：**手写 RFC 6455**（标准库没有 WS）

### 2.1 目录

```
web/serve.py                入口：起静态服务 + 终端服务
web/server/pty_base.py      PTY 接口定义 + 按平台选实现
web/server/pty_unix.py      os.openpty() + fork
web/server/pty_win.py       ctypes 调 ConPTY
web/server/websocket.py     RFC 6455 握手 + 帧编解码
web/server/conn.py          单条连接的生命周期
```

把平台分叉关在 `pty_unix.py` / `pty_win.py` 两个文件里，**接口完全一样**。这样 Windows 那段即使要来回调几轮，也只动一个文件。

### 2.2 启动

```bash
python3 serve.py                          # 8080 静态 + 9003 终端
python3 serve.py --http-port 8080 --term-port 9003 --bind 0.0.0.0
```

`--bind` 默认 `0.0.0.0`，和 `python3 -m http.server` 的默认行为一致（不制造意外差异）。

### 2.3 PTY 层接口

两个实现必须满足同一套语义：

```python
class Pty:
    @staticmethod
    def spawn(argv: list[str], env: dict, cols: int, rows: int) -> "Pty"
    def read(self, max_bytes: int) -> bytes   # 阻塞；返回 b"" 表示 EOF
    def write(self, data: bytes) -> None
    def resize(self, cols: int, rows: int) -> None
    def close(self) -> None                   # 幂等
    @staticmethod
    def supported() -> bool                   # 平台是否支持
```

**刻意不暴露 `fileno()`。** 因为 Windows 上 ConPTY 给的是管道，而 Windows 的 `select()` 只认 socket，暴露 fd 会诱导出"在 Windows 上 select 管道"这种写不通的代码。所有实现都用阻塞读配合线程（见 2.5）。

### 2.4 两条实现

**`pty_unix.py`**

- `os.openpty()` 拿 master/slave
- `os.fork()`；子进程里 `os.setsid()` → `fcntl.ioctl(slave, TIOCSCTTY, 0)` → `os.dup2` ×3 → `os.execvpe()`
- `env` 里设 `TERM=xterm-256color`、`COLORTERM=truecolor`
- `resize` → `fcntl.ioctl(master, TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))`
- `close` → `os.killpg(pgid, SIGHUP)`，然后 `waitpid` 收尸

**`pty_win.py`**

用 `ctypes` 调 Win32，没有封装，全是原始 API：

- `CreatePipe` × 2：一条给 ConPTY 喂输入，一条读它的输出
- `CreatePseudoConsole(COORD{cols, rows}, hIn, hOut, 0, &hPC)`
- `InitializeProcThreadAttributeList` + `UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC)`，塞进 `STARTUPINFOEXW`
- `CreateProcessW(..., EXTENDED_STARTUPINFO_PRESENT, ...)`
- 父进程关掉不需要的那几端句柄
- `resize` → `ResizePseudoConsole(hPC, COORD)`
- `close` → `TerminateProcess` + 关句柄

**ConPTY 需要 Windows 10 1809（build 17763）以上。** 更老的版本 `supported()` 返回 `False`，走 4.4 的降级。

### 2.5 线程模型

**每条连接两条阻塞线程，不用 `select`：**

```
线程 A：空转在 Pty.read() 上，读到什么就往 WS 写什么（写侧加锁）
线程 B：空转在 WS recv 上，收到二进制→Pty.write，收到控制消息→Pty.resize
```

这么选是为了平台统一 —— 两条线程各自阻塞，Unix 的 fd 和 Windows 的管道都成立，不需要为了 `select` 再套一层 self-pipe。代价是每格 2 条线程，一个屏幕上几十格也才几十条线程，无所谓。

### 2.6 WS 上的协议

**二进制帧 = 原始字节，文本帧 = JSON 控制消息。** 靠帧类型区分，不靠内容前缀，这样 PTY 的任意字节（含非法 UTF-8）可以直接走二进制帧，不怕被当文本解码。

| 方向 | 帧 | 内容 |
|---|---|---|
| 浏览器 → 服务端 | 二进制 | 键盘输入原始字节 |
| 浏览器 → 服务端 | 文本 | `{"t":"resize","cols":C,"rows":R}` |
| 服务端 → 浏览器 | 二进制 | PTY 输出原始字节 |
| 服务端 → 浏览器 | 文本 | `{"t":"exit","code":N}` —— 子进程退出 |
| 服务端 → 浏览器 | 文本 | `{"t":"error","message":"…"}` —— 例如平台不支持（见 4.4） |

路径只认 `/term`，其它路径回 404。

**写锁**：线程 A 写输出、线程 B 可能在写 pong，两条线程都要写同一个 socket → 写侧必须加锁。

### 2.7 连接与子进程的生命周期

这是最容易漏的地方。

- 浏览器关 WS（关窗格 / 刷新 / 网络断）→ 服务端立即 `Pty.close()`，SIGHUP 掉整个进程组
- 子进程自己退出（敲了 `exit`）→ 服务端发 `{"t":"exit","code":N}`，然后关 WS
- **必须有收尸**：服务端跟踪所有活的子进程，进程退出时 `waitpid` 回收。漏了就会攒一堆僵尸 bash
- 服务端进程退出时，把所有活着的子进程一起带走

## 3. 前端

### 3.1 页面与路由

- `router.js`：`PAGES = ['monitor', 'history', 'terminal']`
- `index.html`：导航加第三个按钮「终端」，新增 `#page-terminal`
- 新增 `web/js/term.js`（经典脚本，不是 module）、`web/css/terminal.css`

### 3.2 布局模型

**二叉树，不是 CSS grid。**

```
node := { kind:'leaf',  id, term, fit, ws, el }
      | { kind:'split', dir:'row'|'col', ratio, a:node, b:node, el }
```

`dir:'row'` = 左右切分（中间一条竖线），`dir:'col'` = 上下切分（中间一条横线）。

为什么不是 grid：Terminator 的核心手感是"**在任意一个已有的格子上再切一刀**"，而且切完两边可以不等分。grid 表达不了这种嵌套不等分；二叉树天然就是它。

DOM：一个 split 节点渲染成一个 flex 容器，顺序是 `[前子][分隔条][后子]` —— `dir:'row'` 时是 `[左][竖条][右]`（`flex-direction: row`），`dir:'col'` 时是 `[上][横条][下]`（`flex-direction: column`）。两个子节点用 `flex-basis` 分别吃 `ratio` 和 `1-ratio`。

### 3.3 交互

- **右键窗格** → 菜单：左右切分 / 上下切分 / 关闭 / 最大化
- **快捷键**（对齐 Terminator）

  | 键 | 动作 |
  |---|---|
  | `Ctrl+Shift+O` | 左右切分 |
  | `Ctrl+Shift+E` | 上下切分 |
  | `Ctrl+Shift+W` | 关闭当前窗格 |
  | `Ctrl+Shift+X` | 最大化 / 还原 |

- ★ **必须拦截**：用 xterm 的 `attachCustomKeyEventHandler`，命中我们的快捷键就返回 `false`（xterm 不处理、也不送进 shell），其余一律放行。不拦的话这些键会被 bash 吃掉，或者反过来把 shell 自己的键抢了。
- **分隔条拖动**：`pointerdown` 抓取 → `pointermove` 按容器矩形算 `ratio` → 钳制最小尺寸（宽 ≥ 80px / 高 ≥ 60px）
- **至少留一格**：最后一格的右键菜单里不显示"关闭"；`Ctrl+Shift+W` 在只剩一格时无效
- **最大化**：不重建 DOM，只标记路径 —— 记录 `maximized` 指针，沿着根到该叶子的路径，给沿途每个 split 加类、给每个"旁支"兄弟加 `.max-hidden`（`display:none`）。还原时清掉。切换后要重新 `fit()`

### 3.4 xterm 集成

- **vendor 到 `web/lib/xterm/`**：`xterm.min.js`、`xterm.css`、`addon-fit.min.js`（来自 `@xterm/xterm` 与 `@xterm/addon-fit`，一次性下载、之后跟随仓库，和 gsap / three 一个做法）
- xterm 是 UMD 经典脚本 → 不碰 import map，和现有脚本同一层引入
- 每格一个 `Terminal` + 一个 `FitAddon`
- **配色接现有深色板**：xterm 支持完整 theme 对象（background / foreground / cursor / ANSI 16 色），照 `app.css` 的色板填。这是终端页观感能拉开差距的唯一地方

### 3.5 尺寸 —— 会踩的那个坑

`Router` 切页只切 `hidden`，**不重建 DOM**（这是既有约定，不能破）。带来的后果：

- 页面 `hidden` 时所有元素尺寸为 0 → `FitAddon` 会算出 **0 列 0 行**，把终端尺寸写坏
- **这和 chart.js 踩过的坑同源**，处理方式也照抄：`ResizeObserver` 回调里**尺寸为 0 直接 return**，不写尺寸

所以：

- 隐藏期间不 fit
- 切回终端页时，在 `Router.onChange` 里统一 `fitAll()`
- 最大化/还原后、窗口 resize 后，同样 `fitAll()`

## 4. 生命周期与边界

### 4.1 切页不杀 shell

离开终端页 shell 继续跑，回来接着用 —— 和监控页的波形、3D 一样，靠"不重建 DOM"实现。

### 4.2 断开与重连

- WS 断开 → 该格盖一层遮罩"已断开"，带一个「重连」按钮
- **不自动重连**：自动重连会悄悄起一堆新 shell，旧的还不知道死没死
- 「重连」= 建一条新 WS = **新开一个 shell**（旧的那个已经随连接关闭被 SIGHUP 了，这是对的）

### 4.3 刷新页面

所有 shell 没了。正常 —— 等价于 Terminator 关掉整个窗口。

### 4.4 平台降级

- **Windows 10 1809 以下**：`Pty.supported()` 为 `False`。`serve.py` 静态服务照常，终端 WS 回一条 `{"t":"error","message":"当前平台不支持终端"}`，前端在终端页显示一行说明。监控页、历史数据页不受影响
- **非 /term 路径**：404

### 4.5 xterm 加载失败

终端页显示一行提示，不影响另外两页（`term.js` 全程判空降级，和 `anim.js` 对 GSAP 的处理同一个套路）。

## 5. 文件清单

```
新增  web/serve.py
新增  web/server/pty_base.py, pty_unix.py, pty_win.py, websocket.py, conn.py
新增  web/js/term.js
新增  web/css/terminal.css
新增  web/lib/xterm/…                 （vendor）
新增  web/tests/term_client.js        （Node 原生 WebSocket 对拍脚本）
新增  web/tests/win_smoke.md          （Windows 手测步骤）
修改  web/index.html                  导航 + 第三页 + 引入
修改  web/js/router.js                PAGES 加 'terminal'
修改  web/README.md                   启动方式
修改  DEPLOY.md                       启动方式 + Windows 说明
不动  src/**                          下位机零改动
```

**启动方式的变化**（README / DEPLOY 都要改）：

```
旧：  cd web && python3 -m http.server 8080
新：  cd web && python3 serve.py
```

## 6. 测试

### 6.1 后端（自动，本机可跑）

`web/tests/term_client.js`，用 Node 24 的原生 WebSocket（仓库里 `test-client.js` 已有先例），断言：

1. 连 `/term`，发 `echo HELLO\n`，回显里含 `HELLO`
2. 先发 `{"t":"resize","cols":100,"rows":30}`，再发 `stty size\n`，回显是 `30 100`
3. **大输出 / 分片**：`head -c 200000 /dev/urandom | base64` → 字节数对得上，不丢不炸（这是手写 WS 最容易翻车的地方）
4. 非 `/term` 路径被拒绝
5. 关连接后 `pgrep` 数不到残留 bash（与基线对比）—— 验 2.7 的收尸

### 6.2 前端（自动，headless Chrome）

用我们已经跑通的那套：

1. 点右键菜单"左右切分" → `.term-pane` 数量 +1
2. 拖分隔条 → 两个子容器的 `flex-basis` 真的变了
3. 切到监控页再切回来 → 窗格内 `echo $$` 前后一致（shell 没被重建、没被 fit 成 0 列）
4. 只剩一格时，关闭项消失

> headless 的坑照旧：注入 `*{transition:none !important}`（虚拟时间下 CSS 过渡不推进）、`--enable-unsafe-swiftshader`。

### 6.3 Windows ConPTY（人工，只有你能做）

`web/tests/win_smoke.md` 列手测步骤：起 `serve.py` → 打开终端页 → 出现提示符 → `dir` 有输出 → 拖大窗格后 `mode con` 跟着变 → `exit` 后窗格显示已退出 → 关窗格后任务管理器里没有残留进程。

**这段代码我在这台 Linux 上写得了、测不了**，只能靠你在 Windows 上跑几轮反馈。

## 7. 不做的（YAGNI）

布局保存与恢复 · 标签页 · 广播输入（Terminator 有，用不上）· 终端鉴权（按你的要求不做）· 内建 SSH 客户端（shell 里自己敲）· 多用户/多会话隔离 · Windows 10 1809 以下的 ConPTY 兼容

## 8. 已知风险

| # | 风险 | 应对 |
|---|---|---|
| 1 | **Windows ConPTY 那段无法在本机验证** | 接口隔离在 `pty_win.py` 一个文件；出问题只动它；给你一份手测清单 |
| 2 | 手写 WebSocket 的边界（分片 / 掩码 / >64KB 帧 / 控制帧交错） | 6.1 的第 3 条专门撞它 |
| 3 | 隐藏页把终端 fit 成 0 列 | 尺寸为 0 时直接 return；`Router.onChange` 统一重测（chart.js 同源坑） |
| 4 | `Ctrl+Shift+*` 在终端里有歧义 | `attachCustomKeyEventHandler` 明确拦截我们的，其余放行 |
| 5 | ConPTY 需要 Win10 1809+ | `supported()` 检测版本，更老的走 4.4 降级 |

## 9. 安全说明（一句）

这条通道就是**上位机上的一个 shell，没有鉴权**（按你的要求）。所以上位机应处在一个受控的实验室内网里，别把 9003 暴露到公网。

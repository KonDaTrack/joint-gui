# 终端页设计 —— Terminator 式切分

> 2026-10-11 · 状态：**已实现**（提交 `81e9614`）。§0 末尾记了 Windows 支持被删掉的经过。

## 0. 由来

监控页、历史数据页之外，再加第三个页面「终端」。要求像 Terminator 一样能上下左右切分。

过程中定下的四件事：

1. **终端由 `serve.py` 提供，跑在哪台机器上是自由的**（推荐直接放在板子上）。下位机（Qt / 9002）一行都不改。
2. **每个窗格是一个独立 shell**，切一下多开一个。就是 Terminator 的原义。
3. 窗格里的 shell 用来干什么**不归这个界面管** —— 你可以在里面对任意主机敲 `ssh 用户名@地址`。
4. **上位机侧什么都不用装**，只需要一个现代浏览器。

### 修订：Windows 支持被删掉了

最早为了让"`serve.py` 万一跑在 Windows 上"也能用，写了第二条 PTY 实现
（`pty_win.py`，用 ctypes 调 ConPTY，约 250 行）。

后来部署形态定死了：**`serve.py` 跑在 ARM 板（Linux）上，Windows 那边只开浏览器**。
ConPTY 于是没有任何部署路径，而那段代码在开发机上根本跑不到、也就无从验证 ——
留着一块"看着能用、没人验过"的代码比删掉更危险。所以连同它的手测清单一起删了，
`pty_base` 也收敛成只有一条 Unix 实现。

**真要恢复，git 里有**（提交 `81e9614`）。

## 1. 架构

```
浏览器（任何一台机器，只要能进这个局域网）
   │   静态文件和终端 WebSocket 走**同一个端口**
   │     http://<板子>:8080/        ← 页面
   │     ws://<板子>:8080/term      ← 每格一条
   ▼
板子 · serve.py     ← 它取代 python3 -m http.server
   ├─ :8080/      静态文件（行为与 http.server 一致）
   └─ :8080/term  PTY 桥：每条连接 os.forkpty() 一个 shell
```

### 两个"零"

**下位机零改动。** 终端整套东西都在 `serve.py` 里，和 EtherCAT 那条链路完全隔离 —— 不碰 9002、不碰 Qt、不碰 `src/`。

**上位机零部署。** `serve.py` 跑在哪台机器上，终端里的 shell 就是哪台机器的。**推荐直接跑在板子上**，这样：

- 上位机只剩"开个浏览器"这一件事 —— Windows / Mac / Linux / 平板都行（要求见 4.6）
- **地址推导永远正确**：页面从板子发出去，浏览器就会自动去连板子的 9002，`?host=` 和开场画面那个地址框从此都用不上
- 终端里就是**板子自己的 shell** —— 看 `dmesg`、改配置，本来就在板子上，不用 ssh。要 ssh 去别处，在那个 shell 里敲就行
- 板子上 Qt 界面和网页界面并存，靠现有控制权仲裁分工：本机有人操作，别人用浏览器同时看

当然也可以放工控机或任何一台 Linux 机器上，终端就变成那台机器的 shell。**位置是自由的，这一节剩下的部分不依赖它。**

`serve.py` 是那台机器上唯一的常驻进程，静态服务和终端服务同生共死 —— 启动方式从"两条命令"变成"一条"。

## 2. 后端 `web/serve.py`

**Python 3.12 纯标准库，零 pip 依赖。** 试验台是离线环境，装不了包，所以：

- 静态服务：`http.server.ThreadingHTTPServer`，行为对齐 `python3 -m http.server`
- PTY：`os.forkpty()`（见 2.4）
- WebSocket：**手写 RFC 6455**（标准库没有 WS）

### 2.1 目录

```
web/serve.py                入口：起静态服务 + 终端服务
web/server/pty_base.py      PTY 接口 + 环境准备（TERM 等）
web/server/pty_unix.py      os.forkpty()
web/server/websocket.py     RFC 6455 握手 + 帧编解码
web/server/conn.py          单条连接的生命周期
```

`pty_unix` 是**延迟 import** 的（`pty_base._unix()`）：它依赖 `fcntl` / `termios`（Unix 独有），
放模块顶层会让 `serve.py` 在没有这些模块的平台上**直接起不来** ——
而终端不可用的时候，静态服务本来应该照常提供（见 4.4）。

### 2.2 启动

```bash
python3 serve.py                          # 0.0.0.0:8080，静态 + 终端同端口
python3 serve.py --port 8080 --bind 0.0.0.0
```

**只有一个端口。** 静态文件和终端同住 8080：普通 HTTP 请求走静态文件，带 `Upgrade: websocket` 且路径为 `/term` 的走终端。上位机只需要通这一个端口，终端 WS 也因此是**同源**的（见 3.1）。

`--bind` 默认 `0.0.0.0`，和 `python3 -m http.server` 的默认行为一致（不制造意外差异）。

### 2.3 PTY 层接口

```python
def spawn(argv, env, cols, rows) -> Pty
def supported() -> bool

# Pty 实例：
def read(n) -> bytes        # 阻塞；b"" 表示对端结束
def write(data)
def resize(cols, rows)
def close()                 # 幂等，且必须把子进程收干净
```

**刻意不暴露 `fileno()`。** 唯一的使用方式是"阻塞读 + 每条连接两条线程"（见 2.5）。
不暴露 fd，就没人能在实现里悄悄引入 `select` —— 那是给未来的自己留的坑。

### 2.4 实现：`os.forkpty()`

- `os.forkpty()` 一次把 pty、fork、`setsid`、`TIOCSCTTY`、`dup2` 全做了，
  交给父进程一个 master fd
- ⚠️ **不用 `pty.spawn()`**：它会把整个进程接管掉（自己进阻塞循环直到子进程退出），
  我们要的是把 master fd 交回来自己管
- ⚠️ **不用 `openpty` + `subprocess`**：少了 `TIOCSCTTY` 就没有控制终端，
  Ctrl+C / Ctrl+Z 这些作业控制会失效；而 subprocess 没法在子进程里做 ioctl
  （除非 `preexec_fn`，那个更不安全）
- 子进程里**只做 `execve` 一件事**，可执行文件路径在父进程用 `shutil.which` 解析好 ——
  fork 出在多线程进程里，子进程越少碰 Python 越好
- `env` 里设 `TERM=xterm-256color`、`COLORTERM=truecolor`；**删掉 `LINES`/`COLUMNS`**
  （它们是"上一次的尺寸"，留着会让 shell 用陈旧值）
- `resize` → `fcntl.ioctl(master, TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))`
- `close` → 顺序是 **先 `SIGHUP` 整个进程组，等它退出，再关 fd**。
  反过来会让阻塞在 `read` 上的线程醒不过来。
  杀**进程组**而不是单个 pid：shell 里可能还挂着 ssh、top 之类它自己开的子进程

> 顺带一提：Python 3.12 起，多线程进程里调 fork 会报 `DeprecationWarning`。
> 那个告警针对的是"fork 完还在子进程里跑 Python"的用法；我们 fork 完立刻 `execve`，
> 正是 forkpty 被设计出来干的使用方式，所以代码里显式忽略了这个告警。

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

终端只认 `/term` 这一个路径。别的路径照常当静态文件处理（找不到就 404）。

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
- **终端地址是同源的**：`ws://${location.host}/term` —— 因为静态和终端同端口（见 2.2）。**不用写死主机名和端口**，页面搬到哪台机器都自动跟着走

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

- **顶部工具条**：左右切分 / 上下切分 / 最大化 / 关闭窗格，作用于**当前窗格**
  （设计时只写了右键菜单，实现时补了这条 —— 菜单得"先想到去右键"才发现得了，
  工具条把动作摊开，右键菜单和快捷键照旧都在）
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

- **非 Unix 平台**（没有 `os.forkpty`）：`pty_base.supported()` 为 `False`。
  `serve.py` 静态服务照常，`/term` 回一条 `{"t":"error","message":"当前平台没有 PTY…"}`，
  前端在终端页显示一行说明 —— 监控页、历史数据页完全不受影响
- `serve.py` 启动时打一行警告，但**照常提供服务** —— 不能因为终端不可用就让整个界面起不来

> 这个分支在推荐部署下**永远走不到**（`serve.py` 跑在板子/Linux 上）。
> 留着它是为了让"有人把 serve.py 起在别处"时有一个体面的失败，而不是整个界面打不开。

### 4.5 xterm 加载失败

终端页显示一行提示，不影响另外两页（`term.js` 全程判空降级，和 `anim.js` 对 GSAP 的处理同一个套路）。

### 4.6 浏览器要求（上位机侧）

上位机唯一的要求就是这个：

| 能力 | 用在哪 | 最低版本 |
|---|---|---|
| import map | three.js 的 ESM 加载（三维关节） | Chrome/Edge 89+、Firefox 108+、Safari 16.4+ |
| WebGL2 | 三维关节 | 同期浏览器都有 |
| WebSocket | 全部数据 | 自古就有 |

这两年的浏览器都满足。IE 和很老的版本不行。

**手机能打开，但会挤** —— 界面是按 1920×1080 设计的，没有做响应式。平板横屏勉强，笔记本最舒服。要真在手机上用得另做一套布局，不在这次范围内。

## 5. 文件清单

```
新增  web/serve.py
新增  web/server/pty_base.py, pty_unix.py, websocket.py, conn.py
新增  web/js/term.js
新增  web/css/terminal.css
新增  web/lib/xterm/…                 （vendor：xterm.js + addon-fit + css）
新增  web/tests/term_client.js        （Node 原生 WebSocket 对拍脚本，自己起 serve.py）
新增  web/tests/ui_term.js            （playwright + 系统 Chrome 的无头验证）
修改  web/index.html                  导航 + 第三页 + 引入
修改  web/js/router.js                PAGES 加 'terminal'
修改  web/README.md                   启动方式 + 终端页说明
修改  DEPLOY.md                       §3.5 浏览器界面部署
修改  .gitignore                      __pycache__ / 测试截图
不动  src/**                          下位机零改动
```

**启动方式的变化**（README / DEPLOY 都要改）：

```
旧：  cd web && python3 -m http.server 8080
新：  cd web && python3 serve.py
```

**部署**：`web/` 这份文件要跟着到板子上（`git pull` 下来就在）。板子上两个进程分开起：

```bash
sudo ./run.sh            # ① Qt 下位机（EtherCAT 要 raw socket，所以 sudo）
cd web && python3 serve.py   # ② 界面服务（普通用户，终端里就是普通 shell）
```

然后**任何一台同网段的机器**打开 `http://<板子IP>:8080` —— 上位机侧什么都不用装、不用配。

## 6. 测试

### 6.1 后端（自动，本机可跑）

```bash
cd joint-gui/web && node tests/term_client.js      # 7 条，全过
```

用 Node 24 的原生 WebSocket（仓库里 `test-client.js` 已有先例），脚本自己起一个
临时端口 + 临时 `$HOME` 的 `serve.py`，跑完自己收。断言：

1. 连 `/term`，发 `echo`，回显正确
2. 先发 `{"t":"resize",...}`，再发 `stty size`，尺寸真的变了 —— 验 `TIOCSWINSZ` 通了
3. **大输出 / 分片**：200000 字节可打印字符，**一字不差**（这是手写 WS 最容易翻车的地方）
4. 带 `Upgrade: websocket` 但路径不是 `/term` 的连接被拒绝
5. 关连接后 `pgrep` 数不到残留 bash（与基线对比）—— 验 2.7 的收尸

> 写这个脚本时踩到的坑记一笔：**标记字面量不能出现在命令回显里**。
> 一开始用 `printf '__WE__'` 当结束标记，而 shell 会回显命令行 —— 于是
> `waitFor` 一看到回显就返回了，那时真正的输出还没开始。改成输出时才拼出来的
> `printf '__W%s__' E` 才对。

### 6.2 前端（自动，playwright + 系统 Chrome）

```bash
cd joint-gui/web
NODE_PATH=<装 playwright 的 node_modules> node tests/ui_term.js    # 18 条，全过
```

1. 工具栏/右键「左右切分」→ 窗格 +1；再「上下切分」→ 嵌套 +1
2. 拖分隔条 → 两侧宽度**真的变了**
3. **切到监控页再切回来 → 窗格内 `echo $$` 前后一致**（shell 没被重建），
   且没有哪一格被 fit 成 0 列 —— 这一条同时验了 3.5 那个坑
4. 只剩一格时，右键菜单的「关闭窗格」是禁用的，工具栏点了也无效
5. 最大化吃满舞台宽，再点还原

> headless 的坑：`--enable-unsafe-swiftshader`（WebGL）；
> 判断界面状态要用 `waitForFunction` 等条件成立，**别用固定 sleep** ——
> cross-slide 切页时新旧两页会同时在场，固定 sleep 会读到假阴性。
> 控制台报错要按 `msg.location().url` 过滤（`/favicon.ico` 的 404 是浏览器自己来要的）。

### 6.3 没有 Windows 测试

曾经有过一份 `win_smoke.md`（ConPTY 手测清单），随 Windows 实现一起删了（见 §0 修订）。

## 7. 不做的（YAGNI）

布局保存与恢复 · 标签页 · 广播输入（Terminator 有，用不上）· 终端鉴权（不做）·
内建 SSH 客户端（shell 里自己敲）· 多用户/多会话隔离 · 非 Unix 平台的 PTY

## 8. 已知风险

| # | 风险 | 应对 |
|---|---|---|
| 1 | 手写 WebSocket 的边界（分片 / 掩码 / >64KB 帧 / 控制帧交错） | 6.1 的第 3 条专门撞它 |
| 2 | 隐藏页把终端 fit 成 0 列 | 尺寸为 0 时直接 return；`Router.onChange` 统一重测（同 chart.js 那个坑） |
| 3 | `Ctrl+Shift+*` 在终端里有歧义（`Ctrl+Shift+E` 在 bash 里另有含义） | 在**捕获阶段**挂 document keydown，命中我们的就 `stopPropagation`，按键根本到不了 shell |
| 4 | 板子上多一个常驻进程，对 EtherCAT 实时性的影响 | 只多几个百分点 CPU；试验台场景不敏感 |
| 3 | 隐藏页把终端 fit 成 0 列 | 尺寸为 0 时直接 return；`Router.onChange` 统一重测（chart.js 同源坑） |
| 4 | `Ctrl+Shift+*` 在终端里有歧义 | `attachCustomKeyEventHandler` 明确拦截我们的，其余放行 |
| 5 | ConPTY 需要 Win10 1809+ | `supported()` 检测版本，更老的走 4.4 降级 |

## 9. 安全说明（一句）

这条通道就是 **`serve.py` 那台机器上的一个 shell，没有鉴权**（按你的要求）。推荐部署下，`serve.py` 跑在板子上，所以它给出去的是**板子的 shell**。

因此板子应处在一个受控的实验室内网里，别把 8080 暴露到公网。另外建议：**`serve.py` 用普通用户起，别跟 Qt 一样用 `sudo`** —— 代码不用改，就是启动方式上分两个进程，这样终端里拿到的是普通 shell 而不是 root shell。

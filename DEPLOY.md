# 部署教程

本教程面向把 `joint_gui` 部署到目标机（x86 工控机 或 aarch64 ARM 板）的操作人员。

## 0. 前置条件
- 目标机是 Ubuntu（或 Debian 系），x86_64 或 aarch64
- 关节模组与相应总线硬件：
  - EtherCAT：目标机网卡直连关节 EtherCAT 口（网卡建议独立专用，勿用主管理网口）
  - CANopen：USB-CAN 适配器（Canable/ControlCAN 类）插入目标机
- 对应架构的 SDK 目录（不在 git 仓库内，需单独放置到 `joint-gui` 的上一级）

## 0.1 从 GitHub 拉取代码（git 工作流）
- 代码仓库：https://github.com/KonDaTrack/joint-gui（公开仓库，拉取免认证）
- **首次克隆**（在目标机）：
  ```bash
  git clone https://github.com/KonDaTrack/joint-gui.git
  # 把对应架构的 aarch64 SDK 目录放到克隆出的 joint-gui 上一级（同级）：
  cp -r /路径/eyou_ethercat_phu_sdk_aarch64_linux_gnu_20260708 ../joint-gui/../
  cp -r /路径/eyou_canopen_sdk_PHU_aarch64_linux_gnu_20260710 ../joint-gui/../
  cd joint-gui && chmod +x build.sh run.sh && ./build.sh
  ```
- **日常更新**（只拉代码，SDK 在板子上不受影响）：
  ```bash
  cd joint-gui && git pull
  ```
- **开发机推送**：`git add . && git commit -m "..." && git push`（已配 SSH 免密）

## 1. 开发机（x86）本机构建与调试
```bash
sudo apt install qtbase5-dev cmake g++ pkg-config
cd joint-gui
./build.sh
# 无硬件也能跑：启动后选"自动检测"→ 无设备自动回退仿真模式
./run.sh
```
> 仿真模式用于教学演示与界面调试，无需接任何硬件。

## 2. 生成 ARM 可执行文件（x86 上交叉编译）
在 x86 开发机一次性配置 multiarch：
```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install cmake gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
                 qtbase5-dev-tools qtbase5-dev:arm64 \
                 libqt5websockets5-dev:arm64
cd joint-gui
./build.sh arm          # 产出 build-arm/joint_gui（aarch64 原生二进制）
```
> 交叉编译链接的是仓库内 aarch64 SDK 库（`eyou_*_aarch64_*` 目录）。

## 3. 部署到 ARM 板
### 3.1 拷贝程序与 SDK 库
> `target` 是占位符，替换为目标板地址，例如 `user@192.168.1.10`（或已配置的 ssh 别名）。
> `/opt` 属 root 所有，scp 不会自动创建父目录，先在板上建目录：

在开发机上把以下文件拷到目标板同一目录（例如 `/opt/joint-gui/`）：
```bash
ssh target 'sudo mkdir -p /opt/joint-gui'
scp build-arm/joint_gui target:/opt/joint-gui/
scp -r ../eyou_ethercat_phu_sdk_aarch64_linux_gnu_20260708/lib target:/opt/joint-gui/eth_lib/
scp -r ../eyou_canopen_sdk_PHU_aarch64_linux_gnu_20260710/lib target:/opt/joint-gui/can_lib/
```
二进制里记录的 SDK 库路径是构建机上的绝对路径，目标机上不会自动找到；
运行时必须用 `LD_LIBRARY_PATH` 显式指向 SDK 库目录（见 3.3）。

### 3.2 安装 Qt 运行库（板上仅需运行库，无需开发环境）
```bash
sudo apt install libqt5widgets5 libqt5gui5 libqt5core5a libqt5network5 libqt5websockets5
```

### 3.3 运行
用 `run.sh` 一键启动（自动按当前架构聚合 SDK 库路径，无需手动 `LD_LIBRARY_PATH`）：
```bash
cd /opt/joint-gui
./run.sh           # 已 setcap 的普通用户
sudo ./run.sh      # 直接 root 启动（EtherCAT 需 raw socket 权限，最简单）
```

**默认无窗口**：启动即自动检测总线并连上，不弹任何对话框、不显示窗口。
界面全在上位机的浏览器里（见 3.5）。启动后日志里会有一行：

```
关节模组下位机已启动（无窗口）。上位机请打开浏览器访问本机的 9002 服务。
```

- **不需要点任何东西** —— 以前要先在连接框里选总线再按确定，上位机才扫得到从站，
  而板子常常就在机柜里，没人会去点
- 需要本地窗口（现场排查、挑仿真、换网卡）时：`./run.sh --ui`
- 板上没有 X/Wayland 也能跑（自动切到 `QT_QPA_PLATFORM=offscreen`）

### 3.4 权限
- **EtherCAT**：SOEM 主站需要 root 或 `CAP_NET_RAW` 访问网卡。最简单用 `sudo ./run.sh`；想免 sudo 就给二进制加 setcap：
  ```bash
  sudo setcap cap_net_raw+ep /opt/joint-gui/joint_gui
  ```
- **CANopen**：USB-CAN 适配器需对应驱动；拔插后确认设备节点存在
  ```bash
  ls /dev/ttyACM* /dev/can* 2>/dev/null   # Canable/SocketCAN 类设备
  ```

### 3.5 浏览器界面（web/）

`web/` 是一套**纯静态**的浏览器界面（监控 / 历史数据 / 终端三页），另加一个
零依赖的 Python 小服务 `serve.py` 负责发文件和终端。

**推荐直接跑在目标机上**，这样：

- 上位机（任何一台有浏览器的机器）**什么都不用装、不用配**，打开网址即可
- 地址推导永远正确（页面从哪来就连哪），不用填下位机地址
- 终端页开出来的 shell 就是**目标机自己的 shell**（`dmesg`、改配置，不用 ssh）
- 目标机上不用开窗口（默认无窗口），界面全在浏览器里

```bash
cd /opt/joint-gui/web        # 或仓库里的 joint-gui/web
python3 serve.py             # 0.0.0.0:8080，静态文件 + 终端
```

★ **不要用 sudo 起 `serve.py`**。终端页里的 shell 就是当前用户的 shell，
用 sudo 起等于把 root shell 交出去。Qt 那边要 sudo，两个进程分开起。

然后在**任何一台同网段的机器**上打开：

```
http://<目标机IP>:8080
```

**前置条件**：

| 项 | 要求 |
|---|---|
| Python | 3.9+（Ubuntu/Debian 自带）。**无 pip 依赖**，离线可用 |
| 浏览器 | Chrome/Edge 89+、Firefox 108+、Safari 16.4+（要 import map 和 WebGL2） |
| 端口 | 只用 **8080**（静态和终端同端口）。别和 Qt 的 9002 撞 |

**上位机侧（Windows / Mac / 平板）什么都不用装** —— 只是个浏览器。
终端页的 shell 是在**目标机**上开出来的，不是在上位机上。

**自检**：打开网址应看到开场画面 → 扫描到从站 → 点「进入」；
切到「终端」页应出现提示符（说明 PTY 通了）。

> 终端是**没有鉴权**的 shell 通道。别把 8080 暴露到公网。

## 4. 快速自检
1. 启动程序 → 选"自动检测" → 连接
2. 若接的是 EtherCAT：状态栏应显示"EtherCAT 已连接，从站数 1"
3. 若接的是 CANopen：状态栏显示"CANopen 已连接，从站数 1"
4. 都没有：状态栏显示"未检测到 CANopen 设备，使用仿真模式"，界面仍可完整演示

## 5. 常见问题（FAQ）
| 现象 | 排查 |
|------|------|
| 自动检测一直落到仿真，但明明接了 EtherCAT | `ip link` 确认网卡名与直连；网卡需独立专用；EtherCAT 需 root/CAP_NET_RAW（见 3.4 setcap），否则检测会跳过该网卡 |
| 交叉编译找不到 Qt5 | 确认装了 `qtbase5-dev:arm64`；toolchain 使用 `cmake/aarch64-linux-gnu.cmake` |
| 运行时报找不到 .so | 检查 LD_LIBRARY_PATH 是否包含 SDK lib 目录；`ldd joint_gui` 看缺失项；Qt 库缺失则装 `libqt5network5` 等运行库 |
| 用 sudo 启动后找不到 SDK .so | 用 `sudo ./run.sh`（run.sh 内部会设置 LD_LIBRARY_PATH，见 3.3） |
| CANopen 无设备 | 确认适配器插入且节点 ID 与界面从站 ID 一致；Canable/SocketCAN 接口可用 `candump can0` 看报文 |
| 使能后不动 | 检查操作模式是否支持；MIT 模式下目标范围需在 SDK 读到的限制内（±12.5 rad 量级） |
| 网页打不开 / 三维关节不出来 | 确认 `python3 serve.py` 在跑；浏览器版本够新（要 import map + WebGL2，见 3.5） |
| 网页能开但连不上从站 | 网页连的是**发页面的那台机器**的 9002。页面放在 A 机、下位机在 B 机时，得用 `http://B:8080` 打开，或用 `?host=B` |
| 终端页提示"不支持" | `serve.py` 没跑在 Unix 上。PTY 只有 Unix 有（`os.forkpty`），把 serve.py 放回板子/工控机即可 |
| 终端页没反应 | 确认 `serve.py` 是**普通用户**起的、且 8080 端口放通；看 `serve.py` 的控制台有没有 `[term]` 那两行 |

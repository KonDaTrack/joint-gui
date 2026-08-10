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
./build/joint_gui
```
> 仿真模式用于教学演示与界面调试，无需接任何硬件。

## 2. 生成 ARM 可执行文件（x86 上交叉编译）
在 x86 开发机一次性配置 multiarch：
```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install cmake gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
                 qtbase5-dev-tools qtbase5-dev:arm64
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
sudo apt install libqt5widgets5 libqt5gui5 libqt5core5a libqt5network5
```

### 3.3 运行
> 注意：**不要用 `sudo ./joint_gui`**——sudo 默认会清空 `LD_LIBRARY_PATH`，导致找不到 SDK `.so`。
> 普通用户直接运行即可；EtherCAT 若需权限，先按 3.4 执行 setcap。

```bash
cd /opt/joint-gui
export LD_LIBRARY_PATH=$PWD/eth_lib:$PWD/can_lib
./joint_gui
```
若确需 root 运行，用 `sudo env LD_LIBRARY_PATH=$PWD/eth_lib:$PWD/can_lib ./joint_gui`。

### 3.4 权限
- **EtherCAT**：SOEM 主站需要 root 或 `CAP_NET_RAW` 访问网卡。推荐用 setcap 让普通用户也可运行（否则每次启动都要 root）：
  ```bash
  sudo setcap cap_net_raw+ep /opt/joint-gui/joint_gui
  ```
- **CANopen**：USB-CAN 适配器需对应驱动；拔插后确认设备节点存在
  ```bash
  ls /dev/ttyACM* /dev/can* 2>/dev/null   # Canable/SocketCAN 类设备
  ```

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
| 用 sudo 启动后找不到 SDK .so | sudo 会清空 LD_LIBRARY_PATH；改用 `./joint_gui`（先 setcap）或 `sudo env LD_LIBRARY_PATH=... ./joint_gui`（见 3.3） |
| CANopen 无设备 | 确认适配器插入且节点 ID 与界面从站 ID 一致；Canable/SocketCAN 接口可用 `candump can0` 看报文 |
| 使能后不动 | 检查操作模式是否支持；MIT 模式下目标范围需在 SDK 读到的限制内（±12.5 rad 量级） |

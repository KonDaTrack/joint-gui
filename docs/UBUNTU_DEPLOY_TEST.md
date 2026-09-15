# Ubuntu 部署与测试教程（快速版）

面向**新换的 Ubuntu 机器**，从零到能控制关节。重点：**网口检测**——换了设备/网卡，EtherCAT 网口名会变（`enx...`/`eth0`/`enpXsY`），必须先确认用哪个口。

## 0. 目录结构（SDK 在 joint-gui 上一级）

```
<父目录>/
├── joint-gui/                     # 本仓库
└── eyou_ethercat_phu_sdk_x86_64_linux_gnu_20260708/   # x86 SDK（aarch64 板用对应目录）
```

## 1. 装依赖

```bash
sudo apt update
sudo apt install -y qtbase5-dev cmake g++ pkg-config ethtool
```

## 2. 获取代码

```bash
git clone https://github.com/KonDaTrack/joint-gui.git
# 或从别处拷贝整个 joint-gui 目录过来
cd joint-gui
chmod +x build.sh run.sh
```

## 3. 检测网口（关键，换了设备网口名必变）

**关节上电，EtherCAT 网线插好**，然后：

```bash
# 1) 列出所有网口和状态
ip -br link
#    USB 转网卡 → enx<MAC>（如 enx00e04c3a41c0）
#    板载千兆口 → eth0 / enpXsY

# 2) 确认哪根网线插着（Link detected: yes = 有网线）
sudo ethtool <口名> | grep "Link detected"
#    对每个看起来可能的口都执行一遍，或拔插网线对比
#    或用：watch -n1 ip -br link   看哪个口 UP/插拔时变

# 3) 让网口处于 UP（SOEM 需要；不用配 IP）
sudo ip link set <口名> up

# 4)（可选但推荐）用探针确认从站在这个口上
g++ -o /tmp/readparams tools/read_joint_params.cpp \
  -I../eyou_ethercat_phu_sdk_x86_64_linux_gnu_20260708/include \
  -L../eyou_ethercat_phu_sdk_x86_64_linux_gnu_20260708/lib \
  -leu_ethercat -lsoem -lcyhcs_log
sudo LD_LIBRARY_PATH=../eyou_ethercat_phu_sdk_x86_64_linux_gnu_20260708/lib \
  /tmp/readparams <口名>
#    输出 slave count: 1  = 从站在，这个口就是 EtherCAT 口
```

> 多个网口时区分：哪个口插上关节网线后 `ethtool` 显示 `Link detected: yes`、且探针能扫到从站，哪个就是 EtherCAT 口。USB 转网卡名字固定是 `enx<MAC>`。

## 4. 编译

```bash
cd joint-gui
./build.sh
```

## 5. 启动

```bash
sudo ./run.sh     # 接 EtherCAT 实机（SOEM 需 raw socket 权限）
./run.sh          # 仿真模式（不接硬件，无需 root）
```
`run.sh` 会自动按架构设置 SDK 库路径。

## 6. 连接

连接对话框：
- 选 **自动检测**（会扫所有 UP 网口找从站），或
- 手动选 EtherCAT、填第 3 步确认的网口名（如 `enx00e04c3a41c0`）。

额定力矩默认 **50**（90mm 关节），如换了关节按需改。

## 7. 测试清单

| 步骤 | 操作 | 期望 |
|------|------|------|
| 1 连接 | 连接对话框确定 | 状态栏「EtherCAT 已连接，从站数 1」，监控开始刷新 |
| 2 使能 | 勾「已确认现场安全」→ 使能 | 驱动状态「运行使能」 |
| 3 PP 位置 | 模式 PP，目标位置 30，下发目标 | 平滑转到 30° 并停住、不抖 |
| 4 PV 速度 | 模式 PV，目标速度 20，下发 | 匀速转；速度 0 停 |
| 5 PT 力矩 | 模式 PT，目标力矩 10，下发 | 空载会转几秒后超速保护停机（正常）；加载后稳定 |
| 6 归零 | 点「归零」 | 状态栏「归零完成」 |
| 7 急停 | 点「急停 ESTOP」 | 快速停机 + 失能，安全勾选复位 |

## 8. 常见问题

| 现象 | 处理 |
|------|------|
| 自动检测落到仿真 | 网口没 UP（第 3 步 `ip link set up`）；或 raw socket 权限（`sudo ./run.sh`）；或网口名填错 |
| 探针 `init failed` | 网口名不对 / 没 UP / 网线没插 / 从站没上电 |
| `pthread_setschedparam: Operation not permitted` | 无害警告，SDK 照常初始化 |
| 力矩模式空载转几秒就停 | 驱动超速保护（3500 rpm），加载测试 |

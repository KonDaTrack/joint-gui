# 关节模组监控台 (joint_gui)

面向职业教育的关节模组 Qt 监控控制界面。支持 EtherCAT / CANopen 双总线与仿真模式。

## 依赖
- Qt5 Widgets 开发包（`qtbase5-dev`）
- CMake ≥ 3.16、g++（C++17）
- 仓库内两套 SDK（EtherCAT / CANopen，x86_64 或 aarch64）

## x86 本机构建
```
sudo apt install qtbase5-dev cmake g++
./build.sh
./build/joint_gui            # 仿真模式可直接运行
```

## ARM 交叉构建（在 x86 上产出 aarch64 可执行文件）
```
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install cmake gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
                 qtbase5-dev-tools qtbase5-dev:arm64
./build.sh arm
# 产出 build-arm/joint_gui（aarch64 原生二进制）
```

## ARM 板上本地构建（备选）
```
sudo apt install qtbase5-dev cmake g++ pkg-config
./build.sh
```

## 部署到 ARM 板
板上仅需 Qt 运行库，无需开发环境：
```
sudo apt install libqt5widgets5 libqt5gui5 libqt5core5a libqt5network5
```
将 `build-arm/joint_gui` 与所需 SDK `.so` 一并拷贝到板上，然后设置
`LD_LIBRARY_PATH` 指向 SDK 的 `lib/` 目录再运行：
```
export LD_LIBRARY_PATH=/path/to/sdk/lib:$LD_LIBRARY_PATH
./joint_gui
```
所需 `.so`：
- EtherCAT: `libeu_ethercat.so` `libsoem.so` `libcyhcs_log.so`
- CANopen: 建议直接拷贝整个 `lib/` 目录（`libeu_canopen.so` `libeu_eds.so` `libeu_candrv.so` `libeu_log.so` `libeu_resources.so` 及 `libcontrolcan.so` `libeu_canable.so` `libusbcanfd.so`，驱动库在运行时按需加载）

## 权限
EtherCAT（SOEM 主站）需 root 或 `CAP_NET_RAW` 访问网卡，用 `sudo` 启动。
CANopen USB-CAN 适配器需对应驱动与权限。

## 单元测试
```
ctest --test-dir build --output-on-failure
```
覆盖：单位换算、仿真关节模型、CANopen MIT 帧编解码。

## 使用
1. 启动 → 选总线（仿真/EtherCAT/CANopen）→ 填参数 → 连接
2. 勾选"已确认现场安全"→ 使能
3. 填目标值 → 下发目标（CSP/CSV/CST/力矩位置混合）
4. 急停/故障复位按需

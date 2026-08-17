# 快速启动（GUI + RS485 模拟输出）

## 1. 编译

```bash
./build.sh
```

## 2. 启动 GUI（EtherCAT）

```bash
# x86（USB 转网卡）
./run.sh

# ARM（板载网口，需要 raw socket 权限）
sudo ./run.sh
```

- 连接对话框：选「自动检测」或填网卡名（ARM 板载口一般 `eth0`，`ip link` 看）。
- 额定力矩默认 **50**（PHU-20H-90-F-B，90mm 关节）。

## 3. RS485 控制模拟量输出（0-10V）

依赖 python3 + pyserial：

```bash
sudo apt install -y python3-serial    # 或 pip install pyserial
```

用法（脚本在 `tools/analog_out.py`）：

```bash
python3 tools/analog_out.py 5.0    # 输出 5.0V
python3 tools/analog_out.py 0.0    # 输出 0V（复位）
```

脚本顶部按你的模块改 5 个参数：

| 参数 | 含义 | 例 |
|---|---|---|
| `PORT` | 串口设备 | 板载 RS485 用 `ls /dev/tty*` 找；USB 转 RS485 是 `/dev/ttyUSB0` |
| `BAUD` | 波特率 | 9600（模块手册为准） |
| `DEVICE_ID` | Modbus 从站地址 | 1 |
| `REGISTER` | 输出通道的保持寄存器 | 0x0000 |
| `SCALE` | 满量程 = 电压×SCALE | 10V→寄存器 10000 则 SCALE=1000 |

> 协议按 **Modbus RTU 功能码 06 写单个保持寄存器** 实现。如果你的模块不是这个协议（或寄存器/量程不同），把模块手册的报文格式发我，我改脚本。

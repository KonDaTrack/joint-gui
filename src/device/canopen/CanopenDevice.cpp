#include "device/canopen/CanopenDevice.h"
#include "core/UnitConverter.h"
#include "eu_canopen.h"
#include <QDateTime>
#include <thread>

static const int kDevIndex = 0;

// 写控制字并轮询状态字直到达到期望状态（对齐示例 transitionAndWait）
static bool driveTransition(quint16 slave, huint16 controlWord, quint16 wantState)
{
    if (canopen_setControlword(kDevIndex, slave, controlWord) != CANOPEN_SUCCESS)
        return false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < 2000) {
        huint16 sw = 0;
        if (canopen_getStatusWord(kDevIndex, slave, &sw) == CANOPEN_SUCCESS
            && (sw & 0x6F) == wantState)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

CanopenDevice::~CanopenDevice()
{
    close();
}

bool CanopenDevice::readMitLimits(quint16 slave)
{
    MitLimits l;
    hint32 v = 0;
    if (canopen_readDirectory(kDevIndex, slave, 0x2160, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.pMin = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2161, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.pMax = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2162, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.vMin = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2163, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.vMax = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2164, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.kpMin = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2165, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.kpMax = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2166, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.kdMin = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2167, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.kdMax = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2168, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.tMin = v * 0.001f;
    if (canopen_readDirectory(kDevIndex, slave, 0x2169, 0x00, canopen_DataType_int32, &v) != CANOPEN_SUCCESS) return false;
    l.tMax = v * 0.001f;
    limitsBySlave_.insert(slave, l);
    return true;
}

// 读取单个节点的参数（专用 getter + 0x608F 编码器分辨率）；失败字段保持 0。
// CANopen 换算用不到这些（rad 系），仅作信息用途存储供后续显示/日志。
void CanopenDevice::readDeviceParams(quint16 slave)
{
    Joint::DeviceParams p;
    huint32 v = 0;
    if (canopen_getMotorRatedTorque(kDevIndex, slave, &v) == CANOPEN_SUCCESS)
        p.ratedTorqueNm = v;   // 假定 N·m；实机验证单位
    huint32 mr = 0, sr = 0;
    if (canopen_getGearRatioMotorRevolutions(kDevIndex, slave, &mr) == CANOPEN_SUCCESS
        && canopen_getGearRatioShaftRevolutions(kDevIndex, slave, &sr) == CANOPEN_SUCCESS) {
        p.gearRatio = sr > 0 ? (double)mr / sr : 0.0;
    }
    huint32 inc = 0, rev = 0;
    if (canopen_readDirectory(kDevIndex, slave, 0x608F, 0x01, canopen_DataType_uint32, &inc) == CANOPEN_SUCCESS
        && canopen_readDirectory(kDevIndex, slave, 0x608F, 0x02, canopen_DataType_uint32, &rev) == CANOPEN_SUCCESS) {
        p.encoderPulsesPerRev = rev > 0 ? (double)inc / rev : (double)inc;
    }
    paramsBySlave_.insert(slave, p);
}

bool CanopenDevice::open(const AppConfig& cfg)
{
    // 将 UI 选择的波特率（kbps）映射到 SDK 枚举（枚举值即 kbps 数值）
    canopen_Baudrate baud = canopen_Baudrate_1000;
    switch (cfg.canBaudrateKbps) {
    case 10:   baud = canopen_Baudrate_10;   break;
    case 20:   baud = canopen_Baudrate_20;   break;
    case 50:   baud = canopen_Baudrate_50;   break;
    case 100:  baud = canopen_Baudrate_100;  break;
    case 250:  baud = canopen_Baudrate_250;  break;
    case 500:  baud = canopen_Baudrate_500;  break;
    case 800:  baud = canopen_Baudrate_800;  break;
    case 1000: baud = canopen_Baudrate_1000; break;
    default:   baud = canopen_Baudrate_1000; break;
    }
    slaveId_ = cfg.slaveId;
    if (canopen_initDLL(canopen_DeviceType_Canable, kDevIndex, baud) != CANOPEN_SUCCESS)
        return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 节点探测：1..127 短超时查询在线状态，在线节点读 MIT 限幅后纳入列表；
    // 硬件上限 3 个从站，找到 3 个即提前结束以控制探测耗时。
    // 实机注意：若 canopen_getNodeState 对真实节点不可靠（启动后才有响应），
    // 可改为直接以 readMitLimits(id) 成功作为在线判据（一次探测即证明节点可控且是关节）。
    slaveList_.clear();
    canopen_NodeState st = canopen_NodeState_Unknown_state;
    for (quint16 id = 1; id <= 127; ++id) {
        if (canopen_getNodeState(kDevIndex, id, &st, 20) == CANOPEN_SUCCESS
            && readMitLimits(id)) {
            slaveList_.append(id);
            readDeviceParams(id);
            if (slaveList_.size() >= 3) break;
        }
    }
    ready_ = !slaveList_.isEmpty();
    if (!ready_) {
        canopen_freeDLL(kDevIndex);
        return false;
    }
    return true;
}

void CanopenDevice::close()
{
    if (ready_) {
        // 先对全部在线节点尽力失能，避免断连时电机仍带电保持目标（教学安全）
        for (quint16 s : slaveList_) {
            canopen_setControlword(kDevIndex, s, 0x07);
            canopen_setControlword(kDevIndex, s, 0x00);
        }
        canopen_freeDLL(kDevIndex);
        ready_ = false;
        slaveList_.clear();        // 保持 slaveCount()/slaveList() 与连接状态一致
        limitsBySlave_.clear();    // 清掉上一会话的每节点限幅
        paramsBySlave_.clear();    // 清掉上一会话的每节点参数
    }
}

bool CanopenDevice::enable(quint16 slave)
{
    if (!ready_) return false;

    // 操作模式：力矩位置混合
    if (canopen_setOperateMode(kDevIndex, slave, canopen_OperateMode_TorquePositionFixed)
        != CANOPEN_SUCCESS)
        return false;
    canopen_setInterpolationTimePeriodValue(kDevIndex, slave, 4);
    canopen_setSyncCounter(kDevIndex, slave, 0);

    // RPDO 配置（对齐示例 test_mit_mode.cpp）
    canopen_setRPDOCobId(kDevIndex, slave, 0, (0x80 << 24) + 0x200 + slave);
    canopen_setRPDOMaxMappedCount(kDevIndex, slave, 0, 0);
    canopen_setRPDOTransmitType(kDevIndex, slave, 0, 0xFF);
    canopen_setRPDOMapped(kDevIndex, slave, 0, 0, (0x2150 << 16) + 0x20);
    canopen_setRPDOMapped(kDevIndex, slave, 0, 1, (0x2151 << 16) + 0x20);
    canopen_setRPDOMaxMappedCount(kDevIndex, slave, 0, 2);

    // NMT 复位并启动
    canopen_setNodeState(kDevIndex, slave, canopen_NMTState_Reset_Node);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    canopen_setNodeState(kDevIndex, slave, canopen_NMTState_Start_Node);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    canopen_setRPDOCobId(kDevIndex, slave, 0, 0x200 + slave);

    // 驱动状态机到 OPERATION_ENABLED（对齐示例 0x06→0x07→0x0F）
    huint16 sw = 0;
    canopen_getStatusWord(kDevIndex, slave, &sw);
    if (Joint::mapDriveState(sw) != Joint::DriveState::OperationEnabled) {
        if (!driveTransition(slave, 0x06, 0x21)) return false;  // Shutdown
        if (!driveTransition(slave, 0x07, 0x23)) return false;  // SwitchOn
        if (!driveTransition(slave, 0x0F, 0x27)) return false;  // EnableOperation
    }
    canopen_setControlword(kDevIndex, slave, 0x1F);
    return true;
}

bool CanopenDevice::disable(quint16 slave)
{
    if (!ready_) return false;
    canopen_setControlword(kDevIndex, slave, 0x07);   // Disable Operation
    canopen_setControlword(kDevIndex, slave, 0x00);   // Switch Off + Disable Voltage
    return true;
}

bool CanopenDevice::quickStop(quint16 slave)
{
    if (!ready_) return false;
    return canopen_setControlword(kDevIndex, slave, 0x03) == CANOPEN_SUCCESS;  // 快速停机位清零
}

bool CanopenDevice::faultReset(quint16 slave)
{
    if (!ready_) return false;
    // CiA402 故障复位：控制字 bit7 (0x80) 上升沿 0→1→0
    canopen_setControlword(kDevIndex, slave, 0x0F);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    canopen_setControlword(kDevIndex, slave, 0x8F);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    canopen_setControlword(kDevIndex, slave, 0x0F);
    return true;
}

bool CanopenDevice::setOperateMode(quint16 slave, Joint::OperateMode mode)
{
    if (!ready_) return false;
    return canopen_setOperateMode(kDevIndex, slave, static_cast<canopen_OperateMode>(mode))
           == CANOPEN_SUCCESS;
}

bool CanopenDevice::setTarget(quint16 slave, const Joint::TargetCommand& cmd)
{
    if (!ready_) return false;
    if (!limitsBySlave_.contains(slave)) return false;
    codec_.setLimits(limitsBySlave_.value(slave));
    uint8_t frame[8] = {0};
    // UI 为 deg 系，MIT 帧为 rad 系
    codec_.pack(UnitConverter::degToRad(cmd.positionDeg),
                UnitConverter::degToRad(cmd.velocityDps),
                cmd.torqueNm, cmd.kp, cmd.kd, frame);
    return canopen_writeCanData(kDevIndex, 0x200 + slave, frame, 8) == CANOPEN_SUCCESS;
}

bool CanopenDevice::readTelemetry(quint16 slave, Joint::Telemetry& out)
{
    if (!ready_) return false;
    hint32 pos = 0, vel = 0;
    hint16 tor = 0;
    huint16 sw = 0;
    canopen_OperateMode mode = canopen_OperateMode_Reserve;

    const bool ok =
        canopen_getActualPos(kDevIndex, slave, &pos) == CANOPEN_SUCCESS &&
        canopen_getActualVelocity(kDevIndex, slave, &vel) == CANOPEN_SUCCESS &&
        canopen_getActualTorque(kDevIndex, slave, &tor) == CANOPEN_SUCCESS &&
        canopen_getStatusWord(kDevIndex, slave, &sw) == CANOPEN_SUCCESS &&
        canopen_getOperateMode(kDevIndex, slave, &mode) == CANOPEN_SUCCESS;
    if (!ok) return false;

    out.slave = slave;
    out.connected = true;
    out.positionDeg = UnitConverter::radToDeg(pos * 0.001);
    out.velocityDps = UnitConverter::radToDeg(vel * 0.001);
    out.torqueNm = tor * 0.001;
    out.temperatureC = 0.0;   // CANopen 无确认的温度接口，UI 显示 N/A
    out.statusWord = sw;
    out.driveState = Joint::mapDriveState(sw);
    out.errorCode = 0;
    huint8 errReg = 0;
    if (canopen_getErrorRegister(kDevIndex, slave, &errReg) == CANOPEN_SUCCESS && errReg) {
        huint32 err = 0;
        // subIndex 1 = 首个错误码（subIndex 0 是错误计数）
        out.errorCode = (canopen_getErrorField(kDevIndex, slave, 1, &err) == CANOPEN_SUCCESS)
                        ? static_cast<quint16>(err) : 0x1000;
    }
    out.operateMode = static_cast<Joint::OperateMode>(mode);
    out.timestampMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool CanopenDevice::readSDO(quint16 slave, quint16 index, quint8 subIndex,
                            void* value, int dataType, int timeout)
{
    if (!ready_) return false;
    return canopen_readDirectory(kDevIndex, slave, index, subIndex,
                                 static_cast<canopen_DataType>(dataType), value, timeout)
           == CANOPEN_SUCCESS;
}

bool CanopenDevice::writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                             const void* value, int dataType, int timeout)
{
    if (!ready_) return false;
    return canopen_writeDirectory(kDevIndex, slave, index, subIndex,
                                  static_cast<canopen_DataType>(dataType),
                                  const_cast<void*>(value), timeout) == CANOPEN_SUCCESS;
}

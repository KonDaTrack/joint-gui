#include "device/ethercat/EthercatDevice.h"
#include "core/UnitConverter.h"
#include "eu_ethercat.h"
#include <QDateTime>

EthercatDevice::~EthercatDevice()
{
    close();
}

// 逐从站按标准 CiA402 对象读取参数；失败字段保持 0
void EthercatDevice::readDeviceParams()
{
    for (quint16 s : slaveList()) {
        Joint::DeviceParams p;
        hint32 t = 0;
        if (eth_readSDO(s, 0x6076, 0x00, &t, eth_DataType_int32, 20) == ETH_SUCCESS)
            p.ratedTorqueNm = t;   // 假定 N·m；实机验证单位（可能 mN·m 需 /1000）
        huint32 v1 = 0, v2 = 0;
        if (eth_readSDO(s, 0x608F, 0x01, &v1, eth_DataType_uint32, 20) == ETH_SUCCESS
            && eth_readSDO(s, 0x608F, 0x02, &v2, eth_DataType_uint32, 20) == ETH_SUCCESS) {
            p.encoderPulsesPerRev = v2 > 0 ? (double)v1 / v2 : (double)v1;
        }
        v1 = v2 = 0;
        if (eth_readSDO(s, 0x6090, 0x01, &v1, eth_DataType_uint32, 20) == ETH_SUCCESS
            && eth_readSDO(s, 0x6090, 0x02, &v2, eth_DataType_uint32, 20) == ETH_SUCCESS) {
            p.gearRatio = v2 > 0 ? (double)v1 / v2 : 0.0;
        }
        paramsBySlave_.insert(s, p);
    }
}

// 某从站参数：读到的有效则用，否则回退到 cfg 手动值
// （pulsesPerRev_/gearRatio_/ratedNm_ 已在 open() 从 cfg 赋值）
Joint::DeviceParams EthercatDevice::paramsFor(quint16 slave) const
{
    auto it = paramsBySlave_.find(slave);
    if (it != paramsBySlave_.end() && it->valid()) return *it;
    Joint::DeviceParams p;
    p.encoderPulsesPerRev = pulsesPerRev_;
    p.gearRatio = gearRatio_;
    p.ratedTorqueNm = ratedNm_;
    return p;
}

bool EthercatDevice::open(const AppConfig& cfg)
{
    pulsesPerRev_ = cfg.encoderPulsesPerRev;
    gearRatio_ = cfg.gearRatio;
    ratedNm_ = cfg.ratedTorqueNm;
    cycleMs_ = cfg.ethCycleMs;
    slaveId_ = cfg.slaveId;

    int slaveCnt = 0;
    if (eth_initDLL(cfg.ethInterface.toUtf8().constData(), cycleMs_, &slaveCnt) != ETH_SUCCESS) {
        inited_ = false;
        return false;
    }
    inited_ = true;
    slaveCount_ = slaveCnt;
    if (slaveCount_ > 0) readDeviceParams();
    // 即使 0 从站也要返回 false（自动检测会跳过此网卡），但网卡需由 close 释放
    return slaveCount_ > 0;
}

QList<quint16> EthercatDevice::slaveList() const
{
    QList<quint16> list;
    for (int i = 1; i <= slaveCount_; ++i) list.append(static_cast<quint16>(i));
    return list;
}

void EthercatDevice::close()
{
    if (inited_) {
        // 多从站安全：断开前对全部从站尽力失能
        const QList<quint16> slaves = slaveList();
        for (quint16 s : slaves) eth_disable(s);
        eth_freeDLL();
        inited_ = false;
        slaveCount_ = 0;
    }
}

bool EthercatDevice::enable(quint16 slave)  { return eth_enable(slave) == ETH_SUCCESS; }
bool EthercatDevice::disable(quint16 slave) { return eth_disable(slave) == ETH_SUCCESS; }
bool EthercatDevice::faultReset(quint16 slave) { return eth_faultReset(slave) == ETH_SUCCESS; }
bool EthercatDevice::quickStop(quint16 slave)  { return eth_quickStop(slave) == ETH_SUCCESS; }

bool EthercatDevice::setOperateMode(quint16 slave, Joint::OperateMode mode)
{
    const bool ok = eth_setOperateMode(slave, static_cast<eth_OperateMode>(mode)) == ETH_SUCCESS;
    if (ok) mode_ = mode;
    return ok;
}

bool EthercatDevice::setTarget(quint16 slave, const Joint::TargetCommand& cmd)
{
    const Joint::DeviceParams p = paramsFor(slave);
    switch (mode_) {
    case Joint::OperateMode::CyclicSyncPosition:
    case Joint::OperateMode::ProfilePosition:
    case Joint::OperateMode::InterpolatedPosition:
        if (cmd.hasPosition) {
            eth_setTargetPosition(slave, (hint32)UnitConverter::degToPulses(
                cmd.positionDeg, p.encoderPulsesPerRev, p.gearRatio));
        }
        eth_setProfileVelocity(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileVelocity, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileAcceleration, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileDeceleration, p.encoderPulsesPerRev, p.gearRatio));
        return true;
    case Joint::OperateMode::CyclicSyncVelocity:
    case Joint::OperateMode::ProfileVelocity:
    case Joint::OperateMode::Velocity:
        if (cmd.hasVelocity) {
            eth_setTargetVelocity(slave, (hint32)UnitConverter::degToPulses(
                cmd.velocityDps, p.encoderPulsesPerRev, p.gearRatio));
        }
        return true;
    case Joint::OperateMode::CyclicSyncTorque:
    case Joint::OperateMode::ProfileTorque:
        if (cmd.hasTorque) {
            eth_setTargetTorque(slave, (hint32)UnitConverter::nmToPermille(
                cmd.torqueNm, p.ratedTorqueNm));
        }
        return true;
    case Joint::OperateMode::TorquePositionFixed:
        // 力矩位置混合：位置/速度/力矩三个寄存器同时下发（SDK 无独立 MIT 函数）
        if (cmd.hasPosition) {
            eth_setTargetPosition(slave, (hint32)UnitConverter::degToPulses(
                cmd.positionDeg, p.encoderPulsesPerRev, p.gearRatio));
        }
        if (cmd.hasVelocity) {
            eth_setTargetVelocity(slave, (hint32)UnitConverter::degToPulses(
                cmd.velocityDps, p.encoderPulsesPerRev, p.gearRatio));
        }
        if (cmd.hasTorque) {
            eth_setTargetTorque(slave, (hint32)UnitConverter::nmToPermille(
                cmd.torqueNm, p.ratedTorqueNm));
        }
        return true;
    default:
        return true;
    }
}

bool EthercatDevice::readTelemetry(quint16 slave, Joint::Telemetry& out)
{
    const Joint::DeviceParams p = paramsFor(slave);
    hint32 pos = 0, vel = 0, temp = 0;
    hint16 tor = 0;
    huint16 sw = 0, err = 0;
    eth_OperateMode mode = eth_OperateMode_Reserve;

    const bool ok =
        eth_getActualPosition(slave, &pos) == ETH_SUCCESS &&
        eth_getActualVelocity(slave, &vel) == ETH_SUCCESS &&
        eth_getActualTorque(slave, &tor) == ETH_SUCCESS &&
        eth_getStatusWord(slave, &sw) == ETH_SUCCESS &&
        eth_getOperateMode(slave, &mode) == ETH_SUCCESS &&
        eth_getErrorCode(slave, &err) == ETH_SUCCESS;
    eth_getDriveTemper(slave, &temp);   // 温度读取允许失败

    if (!ok) return false;

    out.slave = slave;
    out.connected = true;
    out.positionDeg = UnitConverter::pulsesToDeg(pos, p.encoderPulsesPerRev, p.gearRatio);
    out.velocityDps = UnitConverter::pulsesToDeg(vel, p.encoderPulsesPerRev, p.gearRatio); // 假定脉冲/s
    out.torqueNm = UnitConverter::permilleToNm(tor, p.ratedTorqueNm);
    out.temperatureC = temp;
    out.statusWord = sw;
    out.driveState = Joint::mapDriveState(sw);
    out.errorCode = err;
    out.operateMode = static_cast<Joint::OperateMode>(mode);
    out.timestampMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool EthercatDevice::readSDO(quint16 slave, quint16 index, quint8 subIndex,
                             void* value, int dataType, int timeout)
{
    return eth_readSDO(slave, index, subIndex, value, static_cast<eth_DataType>(dataType), timeout)
           == ETH_SUCCESS;
}

bool EthercatDevice::writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                              const void* value, int dataType, int timeout)
{
    return eth_writeSDO(slave, index, subIndex, const_cast<void*>(value),
                        static_cast<eth_DataType>(dataType), timeout) == ETH_SUCCESS;
}

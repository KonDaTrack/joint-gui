#include "device/ethercat/EthercatDevice.h"
#include "core/UnitConverter.h"
#include "eu_ethercat.h"
#include <QDateTime>
#include <thread>
#include <chrono>

// 0x6076 额定扭矩原始值 → N·m 的换算系数；实机 PHU 报 250（mN·m）→ 0.25 N·m
static constexpr double kRatedTorqueNmScale = 0.001;

EthercatDevice::~EthercatDevice()
{
    close();
}

// 逐从站读取设备参数。PHU 关节用厂商 OD 布局（实机 0x608F:1/2=524288/1、0x6091:1/2=1/1），
// 与 CANopen 版一致；标准 0x608F:0 / 0x6090 在本驱动不可用。读取失败字段保持 0。
void EthercatDevice::readDeviceParams()
{
    paramsBySlave_.clear();   // 避免重连残留旧从站参数
    for (quint16 s : slaveList()) {
        Joint::DeviceParams p;
        // 0x6076 额定扭矩；单位假定 mN·m（×kRatedTorqueNmScale），实机 PHU 报 250 → 0.25 N·m
        hint32 t = 0;
        if (eth_readSDO(s, 0x6076, 0x00, &t, eth_DataType_int32, 1000) == ETH_SUCCESS)
            p.ratedTorqueNm = t * kRatedTorqueNmScale;
        // 编码器分辨率：优先 0x608F:1/2（分子/分母，PHU 用 524288/1）；sub0 单值作回退
        huint32 v1 = 0, v2 = 0;
        if (eth_readSDO(s, 0x608F, 0x01, &v1, eth_DataType_uint32, 1000) == ETH_SUCCESS
            && eth_readSDO(s, 0x608F, 0x02, &v2, eth_DataType_uint32, 1000) == ETH_SUCCESS) {
            p.encoderPulsesPerRev = v2 > 0 ? (double)v1 / v2 : (double)v1;
        } else {
            huint32 v0 = 0;
            if (eth_readSDO(s, 0x608F, 0x00, &v0, eth_DataType_uint32, 1000) == ETH_SUCCESS && v0 > 0)
                p.encoderPulsesPerRev = v0;
        }
        // 减速比：优先 0x6091:1/2（PHU 厂商布局，1/1=1.0）；回退标准 0x6090:1/2
        v1 = v2 = 0;
        bool gearOk = false;
        if (eth_readSDO(s, 0x6091, 0x01, &v1, eth_DataType_uint32, 1000) == ETH_SUCCESS
            && eth_readSDO(s, 0x6091, 0x02, &v2, eth_DataType_uint32, 1000) == ETH_SUCCESS) {
            p.gearRatio = v2 > 0 ? (double)v1 / v2 : 0.0;
            gearOk = true;
        }
        if (!gearOk) {
            v1 = v2 = 0;
            if (eth_readSDO(s, 0x6090, 0x01, &v1, eth_DataType_uint32, 1000) == ETH_SUCCESS
                && eth_readSDO(s, 0x6090, 0x02, &v2, eth_DataType_uint32, 1000) == ETH_SUCCESS) {
                p.gearRatio = v2 > 0 ? (double)v1 / v2 : 0.0;
            }
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
        paramsBySlave_.clear();   // 与 CANopen 一致，断开后清掉从站参数
    }
}

bool EthercatDevice::enable(quint16 slave)  { return eth_enable(slave) == ETH_SUCCESS; }
bool EthercatDevice::disable(quint16 slave) { return eth_disable(slave) == ETH_SUCCESS; }
bool EthercatDevice::faultReset(quint16 slave)
{
    if (eth_faultReset(slave) == ETH_SUCCESS) return true;
    // 手册推荐：控制字 bit7 上升沿复位（0x0F→0x8F→0x0F）。
    // 用 SDO 直写 0x6040，绕过 eth_setControlWord 在故障态的状态等待。
    huint16 cw = 0x0F;
    eth_writeSDO(slave, 0x6040, 0, &cw, eth_DataType_uint16, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cw = 0x8F;
    eth_writeSDO(slave, 0x6040, 0, &cw, eth_DataType_uint16, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cw = 0x0F;
    eth_writeSDO(slave, 0x6040, 0, &cw, eth_DataType_uint16, 200);
    return true;
}
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
    hint32 pos = 0, temp = 0;
    hint16 tor = 0;
    huint16 sw = 0, err = 0;
    eth_OperateMode mode = eth_OperateMode_Reserve;

    // 速度寄存器(0x606C)在静止时读数不稳（实测静止报 16000），改由位置差分计算；
    // 位置(0x6064)读数经验证稳定。
    const bool ok =
        eth_getActualPosition(slave, &pos) == ETH_SUCCESS &&
        eth_getActualTorque(slave, &tor) == ETH_SUCCESS &&
        eth_getStatusWord(slave, &sw) == ETH_SUCCESS &&
        eth_getOperateMode(slave, &mode) == ETH_SUCCESS &&
        eth_getErrorCode(slave, &err) == ETH_SUCCESS;
    eth_getDriveTemper(slave, &temp);   // 温度读取允许失败

    if (!ok) return false;

    out.slave = slave;
    out.connected = true;
    out.positionDeg = UnitConverter::pulsesToDeg(pos, p.encoderPulsesPerRev, p.gearRatio);
    // 速度 = 位置差分（脉冲/秒 → deg/s），轻滤波抑制编码器量化噪声
    double velDps = 0.0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto itPos = lastPosPulses_.find(slave);
    const auto itTime = lastPosTimeMs_.find(slave);
    if (itPos != lastPosPulses_.end() && itTime != lastPosTimeMs_.end()) {
        const qint64 dt = now - *itTime;
        if (dt > 0) {
            const double dpulses = pos - *itPos;
            const double inst = UnitConverter::pulsesToDeg(dpulses * 1000.0 / dt,
                                                           p.encoderPulsesPerRev, p.gearRatio);
            const auto itVel = lastVelDps_.find(slave);
            velDps = (itVel != lastVelDps_.end()) ? itVel.value() * 0.5 + inst * 0.5 : inst;
            lastVelDps_[slave] = velDps;
        }
    }
    lastPosPulses_[slave] = pos;
    lastPosTimeMs_[slave] = now;
    out.velocityDps = velDps;
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

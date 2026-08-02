#include "device/ethercat/EthercatDevice.h"
#include "core/UnitConverter.h"
#include "eu_ethercat.h"
#include <QDateTime>

EthercatDevice::~EthercatDevice()
{
    close();
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
    // 即使 0 从站也要返回 false（自动检测会跳过此网卡），但网卡需由 close 释放
    return slaveCount_ > 0;
}

void EthercatDevice::close()
{
    if (inited_) {
        // 先尽力失能，避免断连时电机仍带电保持目标（教学安全）
        eth_disable(slaveId_);
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
    switch (mode_) {
    case Joint::OperateMode::CyclicSyncPosition:
    case Joint::OperateMode::ProfilePosition:
    case Joint::OperateMode::InterpolatedPosition:
        if (cmd.hasPosition) {
            eth_setTargetPosition(slave, (hint32)UnitConverter::degToPulses(
                cmd.positionDeg, pulsesPerRev_, gearRatio_));
        }
        eth_setProfileVelocity(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileVelocity, pulsesPerRev_, gearRatio_));
        eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileAcceleration, pulsesPerRev_, gearRatio_));
        eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileDeceleration, pulsesPerRev_, gearRatio_));
        return true;
    case Joint::OperateMode::CyclicSyncVelocity:
    case Joint::OperateMode::ProfileVelocity:
    case Joint::OperateMode::Velocity:
        if (cmd.hasVelocity) {
            eth_setTargetVelocity(slave, (hint32)UnitConverter::degToPulses(
                cmd.velocityDps, pulsesPerRev_, gearRatio_));
        }
        return true;
    case Joint::OperateMode::CyclicSyncTorque:
    case Joint::OperateMode::ProfileTorque:
        if (cmd.hasTorque) {
            eth_setTargetTorque(slave, (hint32)UnitConverter::nmToPermille(
                cmd.torqueNm, ratedNm_));
        }
        return true;
    case Joint::OperateMode::TorquePositionFixed:
        // 力矩位置混合：位置/速度/力矩三个寄存器同时下发（SDK 无独立 MIT 函数）
        if (cmd.hasPosition) {
            eth_setTargetPosition(slave, (hint32)UnitConverter::degToPulses(
                cmd.positionDeg, pulsesPerRev_, gearRatio_));
        }
        if (cmd.hasVelocity) {
            eth_setTargetVelocity(slave, (hint32)UnitConverter::degToPulses(
                cmd.velocityDps, pulsesPerRev_, gearRatio_));
        }
        if (cmd.hasTorque) {
            eth_setTargetTorque(slave, (hint32)UnitConverter::nmToPermille(
                cmd.torqueNm, ratedNm_));
        }
        return true;
    default:
        return true;
    }
}

bool EthercatDevice::readTelemetry(quint16 slave, Joint::Telemetry& out)
{
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
    out.positionDeg = UnitConverter::pulsesToDeg(pos, pulsesPerRev_, gearRatio_);
    out.velocityDps = UnitConverter::pulsesToDeg(vel, pulsesPerRev_, gearRatio_); // 假定脉冲/s
    out.torqueNm = UnitConverter::permilleToNm(tor, ratedNm_);
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

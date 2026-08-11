#include "device/ethercat/EthercatDevice.h"
#include "core/UnitConverter.h"
#include "eu_ethercat.h"
#include <QDateTime>
#include <cmath>
#include <thread>
#include <chrono>

EthercatDevice::~EthercatDevice()
{
    close();
}

// 逐从站读取设备参数。本驱动 OD 的减速比(0x6091=1/1，实物 101)与额定扭矩(0x6076 单位不明)
// 均不可靠，只自动读取编码器分辨率（0x608F:1/2=524288/1，与 19 位编码器一致）。
void EthercatDevice::readDeviceParams()
{
    paramsBySlave_.clear();   // 避免重连残留旧从站参数
    for (quint16 s : slaveList()) {
        Joint::DeviceParams p;
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
        // 减速比、额定扭矩：不自动读取，一律以连接对话框手动设置（cfg）为准
        paramsBySlave_.insert(s, p);
    }
}

// 某从站参数：编码器分辨率用自动读取值；减速比、额定扭矩用 cfg 手动值。
// 本驱动 0x6091(减速比=1/1 实物 101)与 0x6076(额定扭矩单位不明)不可靠。
Joint::DeviceParams EthercatDevice::paramsFor(quint16 slave) const
{
    Joint::DeviceParams p;
    p.encoderPulsesPerRev = pulsesPerRev_;
    p.gearRatio = gearRatio_;
    p.ratedTorqueNm = ratedNm_;
    const auto it = paramsBySlave_.find(slave);
    if (it != paramsBySlave_.end() && it->encoderPulsesPerRev > 0)
        p.encoderPulsesPerRev = it->encoderPulsesPerRev;
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

bool EthercatDevice::enable(quint16 slave)
{
    // 位置类模式使能前，先把目标位置初始化为当前实际位置：
    // 否则使能瞬间"目标位置(旧值/0) vs 实际位置"偏差过大 → 0x8611 位置偏差故障
    if (mode_ == Joint::OperateMode::CyclicSyncPosition
        || mode_ == Joint::OperateMode::ProfilePosition
        || mode_ == Joint::OperateMode::InterpolatedPosition
        || mode_ == Joint::OperateMode::TorquePositionFixed) {
        hint32 pos = 0;
        if (eth_getActualPosition(slave, &pos) == ETH_SUCCESS)
            eth_setTargetPosition(slave, pos);
    }
    return eth_enable(slave) == ETH_SUCCESS;
}
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
    case Joint::OperateMode::InterpolatedPosition:
        // CSP/IP：主站斜坡推进（CSP 用 SDO 每周期写目标会抖，PP 更稳）
        if (cmd.hasPosition) {
            hint32 curPos = 0;
            eth_getActualPosition(slave, &curPos);
            // 0-360 回绕：目标取与当前位置最近的同余角度（走最近方向，最多 ±180°）
            const double curDeg = UnitConverter::pulsesToDeg(
                curPos, p.encoderPulsesPerRev, p.gearRatio);
            double diffDeg = std::fmod(cmd.positionDeg - curDeg + 180.0, 360.0);
            if (diffDeg < 0) diffDeg += 360.0;
            diffDeg -= 180.0;
            const double goal = curPos + diffDeg / 360.0 * (p.encoderPulsesPerRev * p.gearRatio);
            goalPosPulses_[slave] = goal;
            rampVelPulses_[slave] = qMax(1.0, UnitConverter::degToPulses(
                qMax(cmd.profileVelocity, 1.0), p.encoderPulsesPerRev, p.gearRatio));
            if (!cmdPosPulses_.contains(slave))
                cmdPosPulses_[slave] = curPos;
            // 先发当前命令位置（斜坡起点），后续由 readTelemetry 周期推进
            eth_setTargetPosition(slave, (hint32)cmdPosPulses_[slave]);
        }
        eth_setProfileVelocity(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileVelocity, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileAcceleration, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileDeceleration, p.encoderPulsesPerRev, p.gearRatio));
        return true;
    case Joint::OperateMode::ProfilePosition:
    {
        // PP：驱动内部生成平滑轮廓，一次设目标 + 触发新设定点即可，不抖、不报 8611
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
        // 触发新设定点：控制字 bit4 上升沿（0x1F → 0x0F），SDO 直写避开状态等待
        huint16 cw = 0x1F;
        eth_writeSDO(slave, 0x6040, 0, &cw, eth_DataType_uint16, 200);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cw = 0x0F;
        eth_writeSDO(slave, 0x6040, 0, &cw, eth_DataType_uint16, 200);
        return true;
    }
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
    // 0-360 回绕显示：多圈绝对位置取模 360，避免数值累积到几千上万度
    double posDeg = UnitConverter::pulsesToDeg(pos, p.encoderPulsesPerRev, p.gearRatio);
    posDeg = std::fmod(posDeg, 360.0);
    if (posDeg < 0) posDeg += 360.0;
    out.positionDeg = posDeg;
    // 速度 = 位置差分（脉冲/秒 → deg/s），轻滤波抑制编码器量化噪声
    double velDps = 0.0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dtMs = 2;   // 周期估计
    const auto itPos = lastPosPulses_.find(slave);
    const auto itTime = lastPosTimeMs_.find(slave);
    if (itPos != lastPosPulses_.end() && itTime != lastPosTimeMs_.end()) {
        const qint64 dt = now - *itTime;
        if (dt > 0) {
            dtMs = dt;
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
    // 位置斜坡：每周期把命令位置朝目标渐进（限速），CSP 平滑移动避免 0x8611 跟随误差
    const auto itGoal = goalPosPulses_.find(slave);
    if (itGoal != goalPosPulses_.end()) {
        double cmd = cmdPosPulses_.value(slave, (double)pos);
        const double rampVel = rampVelPulses_.value(slave, 1.0);
        const double maxStep = rampVel * dtMs / 1000.0;   // dtMs 毫秒 → 脉冲步长
        const double d = itGoal.value() - cmd;
        cmd = (std::fabs(d) > maxStep) ? cmd + (d > 0 ? maxStep : -maxStep) : itGoal.value();
        cmdPosPulses_[slave] = cmd;
        eth_setTargetPosition(slave, (hint32)cmd);
    }
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

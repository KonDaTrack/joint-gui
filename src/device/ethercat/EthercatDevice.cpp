#include "device/ethercat/EthercatDevice.h"
#include "device/ethercat/JointModelTable.h"
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

namespace {
// SDO 读取带重试。刚 eth_initDLL 完就读取时，邮箱/驱动器可能还没就绪，
// 单次超时失败不代表对象不可读（探针用 2000ms 读同一对象是成功的；
// 1000ms 实测会失败）。重试几次即可，代价只是连接时多花几十毫秒。
bool readSdoRetry(huint16 slave, huint16 index, huint8 sub, void* value,
                  eth_DataType dt, int timeoutMs, int tries)
{
    for (int i = 0; i < tries; ++i) {
        if (eth_readSDO(slave, index, sub, value, dt, timeoutMs) == ETH_SUCCESS)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
} // namespace

// 逐从站读取/识别设备参数。
//  - 编码器分辨率：读 0x608F:1/2（自动读取可靠，与 19 位编码器一致）
//  - 型号：读 0x6076 查型号表 → 得到该从站的减速比与额定力矩（多关节必须按从站区分，
//    三轴额定差 5~9 倍，全局一个值会严重错配）
//  - 0x6091(减速比=2)不可靠，不用；0x6076 不是物理力矩，只作型号指纹
void EthercatDevice::readDeviceParams()
{
    paramsBySlave_.clear();     // 避免重连残留旧从站参数
    modelNameBySlave_.clear();
    modelShortBySlave_.clear();
    modelUnknownBySlave_.clear();
    for (quint16 s : slaveList()) {
        Joint::DeviceParams p;
        // 编码器分辨率：优先 0x608F:1/2（分子/分母，PHU 用 524288/1）；sub0 单值作回退
        huint32 v1 = 0, v2 = 0;
        if (readSdoRetry(s, 0x608F, 0x01, &v1, eth_DataType_uint32, 2000, 3)
            && readSdoRetry(s, 0x608F, 0x02, &v2, eth_DataType_uint32, 2000, 3)) {
            p.encoderPulsesPerRev = v2 > 0 ? (double)v1 / v2 : (double)v1;
        } else {
            huint32 v0 = 0;
            if (readSdoRetry(s, 0x608F, 0x00, &v0, eth_DataType_uint32, 2000, 3) && v0 > 0)
                p.encoderPulsesPerRev = v0;
        }

        // 型号识别：0x6076 三个型号各不相同且随额定单调，作指纹
        huint32 key = 0;
        if (readSdoRetry(s, 0x6076, 0x00, &key, eth_DataType_uint32, 2000, 3)) {
            const JointModelTable::ModelInfo mi =
                JointModelTable::byRatedTorqueKey(static_cast<quint16>(key));
            modelUnknownBySlave_.insert(s, !mi.found);
            if (mi.found) {
                p.ratedTorqueNm = mi.ratedNm;    // 用规格值，不用 0x6076 换算
                p.gearRatio = mi.gearRatio;
                modelNameBySlave_.insert(s, mi.name);
                modelShortBySlave_.insert(s, mi.shortName);
            } else {
                modelNameBySlave_.insert(s, QStringLiteral("未识别(0x6076=%1)").arg(key));
            }
        } else {
            modelUnknownBySlave_.insert(s, true);
            modelNameBySlave_.insert(s, QStringLiteral("未识别(读 0x6076 失败)"));
        }
        paramsBySlave_.insert(s, p);
    }
}

// 某从站参数。识别到型号 → 用该型号的规格值；未识别 → 回退到连接对话框手填值。
Joint::DeviceParams EthercatDevice::paramsFor(quint16 slave) const
{
    Joint::DeviceParams p;
    p.encoderPulsesPerRev = pulsesPerRev_;
    p.gearRatio = gearRatio_;
    p.ratedTorqueNm = ratedNm_;
    const auto it = paramsBySlave_.find(slave);
    if (it != paramsBySlave_.end()) {
        if (it->encoderPulsesPerRev > 0) p.encoderPulsesPerRev = it->encoderPulsesPerRev;
        if (it->gearRatio > 0)           p.gearRatio = it->gearRatio;
        if (it->ratedTorqueNm > 0)       p.ratedTorqueNm = it->ratedTorqueNm;
    }
    return p;
}

Joint::OperateMode EthercatDevice::modeFor(quint16 slave) const
{
    return modeBySlave_.value(slave, Joint::OperateMode::ProfilePosition);
}

// 给界面显示的型号文本。未识别时明确提示"用手填额定值"，
// 避免操作者以为参数已自动配对（额定填错会超发/欠发 5 倍以上）。
QString EthercatDevice::modelInfo(quint16 slave) const
{
    const QString name = modelNameBySlave_.value(slave, QString());
    if (name.isEmpty()) return QString();
    if (modelUnknownBySlave_.value(slave, false))
        return QStringLiteral("%1 · 用手填额定 %2 N·m").arg(name).arg(ratedNm_);
    return QStringLiteral("%1 · 额定 %2 N·m").arg(name).arg(paramsFor(slave).ratedTorqueNm);
}

QString EthercatDevice::modelShortName(quint16 slave) const
{
    return modelShortBySlave_.value(slave, QString());
}

bool EthercatDevice::open(const AppConfig& cfg)
{
    pulsesPerRev_ = cfg.encoderPulsesPerRev;
    gearRatio_ = cfg.gearRatio;
    ratedNm_ = cfg.ratedTorqueNm;
    travelLimitDeg_ = cfg.travelLimitDeg;
    lastCmdVelDps_.clear();      // 重连后不沿用旧命令方向
    lastCmdTorqueNm_.clear();
    modeBySlave_.clear();
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
        // 多从站安全：断开前对全部从站尽力失能。
        // 用带短超时的 SDO 写控制字 0x0000（解除使能）替代 eth_disable：
        // eth_disable 内部走 SDK 默认超时，从站挂死时可能阻塞数秒 → 电机失控时间更长。
        // 写失败（从站无响应）也继续释放网卡，由从站自身 EtherCAT 看门狗触发 Quick Stop。
        const QList<quint16> slaves = slaveList();
        for (quint16 s : slaves) {
            huint16 cw = 0x0000;   // CiA402 控制字：Operation Enabled → Switch On Disabled
            eth_writeSDO(s, 0x6040, 0x00, &cw, eth_DataType_uint16, 200);
        }
        eth_freeDLL();
        inited_ = false;
        slaveCount_ = 0;
        paramsBySlave_.clear();   // 与 CANopen 一致，断开后清掉从站参数
        modelNameBySlave_.clear();
        modelShortBySlave_.clear();
        modelUnknownBySlave_.clear();
        modeBySlave_.clear();
    }
}

bool EthercatDevice::enable(quint16 slave)
{
    const Joint::OperateMode m = modeFor(slave);
    // 位置类模式使能前，先把目标位置初始化为当前实际位置：
    // 否则使能瞬间"目标位置(旧值/0) vs 实际位置"偏差过大 → 0x8611 位置偏差故障
    if (m == Joint::OperateMode::ProfilePosition
        || m == Joint::OperateMode::InterpolatedPosition) {
        hint32 pos = 0;
        if (eth_getActualPosition(slave, &pos) == ETH_SUCCESS)
            eth_setTargetPosition(slave, pos);
    }
    // 轮廓类模式按官方例程用控制字序列使能：0x06(Shutdown) → 0x07(Switch On) → 0x0F(Enable Operation)
    if (m == Joint::OperateMode::ProfilePosition
        || m == Joint::OperateMode::ProfileVelocity
        || m == Joint::OperateMode::ProfileTorque) {
        eth_setControlWord(slave, 0x06);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        eth_setControlWord(slave, 0x07);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        eth_setControlWord(slave, 0x0F);
        return true;
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
    if (ok) modeBySlave_[slave] = mode;
    return ok;
}

// 归航：参考官方 test_zero.cpp。归航方法 0x6098=35，进入 Homing 模式后控制字 bit4 启动，
// 轮询状态字 bit12(归航到位) 完成，写 0x2130 保存参数，最后恢复原操作模式。
bool EthercatDevice::homing(quint16 slave)
{
    hint32 pos = 0;
    if (eth_getActualPosition(slave, &pos) != ETH_SUCCESS) return false;
    if (pos > -20 && pos < 20) return true;   // 已在零位

    huint8 homeMode = 35;
    eth_writeSDO(slave, 0x6098, 0x00, &homeMode, eth_DataType_uint8, 200);

    const Joint::OperateMode prev = modeFor(slave);   // 归航后恢复该从站原模式
    eth_setOperateMode(slave, eth_OperateMode_Homing);
    eth_setControlWord(slave, 0x06);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    eth_setControlWord(slave, 0x07);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    eth_setControlWord(slave, 0x0F);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    eth_setControlWord(slave, 0x0F | 0x10);   // 启动归航（bit4 上升沿，保持 bit0=1）

    // 轮询等待归航完成，超时 10s
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 10000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        huint16 sw = 0;
        if (eth_getStatusWord(slave, &sw) == ETH_SUCCESS && (sw & 0x1000))
            break;   // bit12 homing attained
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    huint8 store = 1;
    eth_writeSDO(slave, 0x2130, 0x00, &store, eth_DataType_uint8, 200);   // 保存 home offset
    eth_setOperateMode(slave, static_cast<eth_OperateMode>(prev));
    modeBySlave_[slave] = prev;
    return true;
}

// 回0：切到 PP 模式，用保守轮廓速度把关节运动到 0 度（相对零点）。
// 与 homing 不同——homing 是标定零点，这里是让关节走回零点。
bool EthercatDevice::moveToZero(quint16 slave)
{
    const Joint::DeviceParams p = paramsFor(slave);
    if (p.encoderPulsesPerRev <= 0 || p.gearRatio <= 0) return false;

    eth_setOperateMode(slave, eth_OperateMode_ProfilePosition);
    modeBySlave_[slave] = Joint::OperateMode::ProfilePosition;
    // 清掉旧方向记录：否则超限时残留的 PV/PT 方向会让限位拦下这次回0
    lastCmdVelDps_[slave] = 0.0;
    lastCmdTorqueNm_[slave] = 0.0;

    // 保守轮廓（回0 是辅助动作，慢一点更安全）：速度 30 deg/s，加/减速 30 deg/s²
    eth_setProfileVelocity(slave, (huint32)UnitConverter::degToPulses(
        30.0, p.encoderPulsesPerRev, p.gearRatio));
    eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
        30.0, p.encoderPulsesPerRev, p.gearRatio));
    eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
        30.0, p.encoderPulsesPerRev, p.gearRatio));

    eth_setTargetPosition(slave, 0);   // 0 度 = 归零后的零点的绝对脉冲位置
    eth_setControlWord(slave, 0x0F | 0x20);        // 立即变更
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    eth_setControlWord(slave, 0x0F | 0x20 | 0x10); // 新设定点上升沿
    return true;
}

// 是否该拦下当前动作：仅当"已越界"且"命令方向继续向外"时为真。
// 反向回范围内的命令一律放行（关节到限位后必须能反着回去）。
bool EthercatDevice::limitBlocksMotion(quint16 slave, double posDeg) const
{
    const bool outPos = (posDeg > travelLimitDeg_);
    const bool outNeg = (posDeg < -travelLimitDeg_);
    if (!outPos && !outNeg) return false;

    const double velCmd = lastCmdVelDps_.value(slave, 0.0);
    const double torCmd = lastCmdTorqueNm_.value(slave, 0.0);
    // 正向越界：继续加正速度/正力矩才算向外；反向越界对称
    return outPos ? (velCmd > 0.0 || torCmd > 0.0)
                  : (velCmd < 0.0 || torCmd < 0.0);
}

bool EthercatDevice::setTarget(quint16 slave, const Joint::TargetCommand& cmd)
{
    const Joint::DeviceParams p = paramsFor(slave);
    switch (modeFor(slave)) {
    case Joint::OperateMode::ProfilePosition:
    case Joint::OperateMode::InterpolatedPosition:
        // 轮廓位置 PP：驱动内部生成平滑轨迹（对齐 test_pp_mode.cpp）
        // PP 有绝对位置目标，不靠速度/力矩判断方向：清掉旧记录，
        // 否则从 PV/PT 切过来时会残留旧方向，让限位误拦 PP 的回程命令
        lastCmdVelDps_[slave] = 0.0;
        lastCmdTorqueNm_[slave] = 0.0;
        eth_setProfileVelocity(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileVelocity, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileAcceleration, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileDeceleration, p.encoderPulsesPerRev, p.gearRatio));
        if (cmd.hasPosition) {
            // 目标角度夹到 ±行程限位内（相对零点带符号），再按绝对角度直接映射成脉冲。
            // 限位下不会超过一圈，无需最近的同余角映射。
            const double target = qBound(-travelLimitDeg_, cmd.positionDeg, travelLimitDeg_);
            eth_setTargetPosition(slave, (hint32)UnitConverter::degToPulses(
                target, p.encoderPulsesPerRev, p.gearRatio));
            // 新设定点：控制字 bit5(立即变更) → bit4(新设定点) 上升沿
            eth_setControlWord(slave, 0x0F | 0x20);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            eth_setControlWord(slave, 0x0F | 0x20 | 0x10);
        } else if (cmd.hasVelocity && cmd.velocityDps == 0.0) {
            // "停止运动"：目标置为当前实际位置并触发新设定点
            hint32 curPos = 0;
            if (eth_getActualPosition(slave, &curPos) == ETH_SUCCESS) {
                eth_setTargetPosition(slave, curPos);
                eth_setControlWord(slave, 0x0F | 0x20);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                eth_setControlWord(slave, 0x0F | 0x20 | 0x10);
            }
        }
        return true;
    case Joint::OperateMode::ProfileVelocity:
    case Joint::OperateMode::Velocity:
        // 轮廓速度 PV：驱动按轮廓加减速生成速度曲线（对齐 test_pv_mode.cpp）
        eth_setProfileAcceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileAcceleration, p.encoderPulsesPerRev, p.gearRatio));
        eth_setProfileDeceleration(slave, (huint32)UnitConverter::degToPulses(
            cmd.profileDeceleration, p.encoderPulsesPerRev, p.gearRatio));
        if (cmd.hasVelocity) {
            eth_setTargetVelocity(slave, (hint32)UnitConverter::degToPulses(
                cmd.velocityDps, p.encoderPulsesPerRev, p.gearRatio));
            lastCmdVelDps_[slave] = cmd.velocityDps;   // 供限位方向判断
            eth_setControlWord(slave, 0x0F);
        }
        return true;
    case Joint::OperateMode::ProfileTorque:
        // 轮廓力矩 PT：驱动按力矩斜率生成力矩曲线（对齐 test_pt_mode.cpp）
        if (cmd.hasTorque) {
            eth_setTorqueSlope(slave, (huint32)qMax(1.0, cmd.torqueSlopeNmPerSec * 1000.0 / p.ratedTorqueNm));
            eth_setTargetTorque(slave, (hint32)UnitConverter::nmToPermille(
                cmd.torqueNm, p.ratedTorqueNm));
            lastCmdTorqueNm_[slave] = cmd.torqueNm;   // 供限位方向判断
            eth_setControlWord(slave, 0x0F);
        } else if (cmd.hasVelocity && cmd.velocityDps == 0.0) {
            eth_setTargetTorque(slave, 0);   // "停止运动"：力矩归零
            lastCmdTorqueNm_[slave] = 0.0;
            eth_setControlWord(slave, 0x0F);
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
    // 遥测用 eth_get* 系列（PDO 路径）：实测本驱动遥测对象走 SDO 短超时读会在连接后立刻失败
    // → 看门狗误判遥测超时断开，故改回 eth_get*（正常时 <1ms）。
    // 代价是从站真挂起时会按 SDK 默认超时阻塞；断开/看门狗走有界 SDO 写失能兜底。
    const bool ok =
        eth_getActualPosition(slave, &pos) == ETH_SUCCESS &&
        eth_getActualTorque(slave, &tor) == ETH_SUCCESS &&
        eth_getStatusWord(slave, &sw) == ETH_SUCCESS &&
        eth_getOperateMode(slave, &mode) == ETH_SUCCESS &&
        eth_getErrorCode(slave, &err) == ETH_SUCCESS;
    if (!ok) return false;
    eth_getDriveTemper(slave, &temp);   // 温度读取允许失败；核心项已成功说明从站存活

    out.slave = slave;
    out.connected = true;
    // 带符号显示（相对归零零点，不回绕）：±行程限位下位置不会超过一圈，
    // 带符号能直观看出偏离零点的方向和距离；越限时数值会超出 ±limit 以醒目提示。
    const double posDeg = UnitConverter::pulsesToDeg(pos, p.encoderPulsesPerRev, p.gearRatio);
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
    out.velocityDps = velDps;
    out.torqueNm = UnitConverter::permilleToNm(tor, p.ratedTorqueNm);
    out.ratedTorqueNm = p.ratedTorqueNm;
    out.temperatureC = temp;
    out.statusWord = sw;
    out.driveState = Joint::mapDriveState(sw);
    out.errorCode = err;
    out.operateMode = static_cast<Joint::OperateMode>(mode);
    // 行程限位保护（保护中空轴内力矩传感器线束）。
    // 放在读遥测里是因为它每周期都跑：PV/PT 没有位置目标，只能靠监控拦；
    // PP 靠 setTarget 里夹紧目标。关键：只拦「继续向外」，反向回范围内的命令必须放行，
    // 否则到了 +170° 就再也回不到 -170°（旧实现每周期强改目标为"保持当前位置"，会把关节钉死）。
    if (limitBlocksMotion(slave, posDeg)) {
        switch (modeFor(slave)) {
        case Joint::OperateMode::ProfilePosition:
        case Joint::OperateMode::InterpolatedPosition:
            eth_setTargetPosition(slave, pos);       // 保持当前位置
            break;
        case Joint::OperateMode::ProfileVelocity:
        case Joint::OperateMode::Velocity:
            eth_setTargetVelocity(slave, 0);         // 速度归零
            lastCmdVelDps_[slave] = 0.0;
            break;
        case Joint::OperateMode::ProfileTorque:
            eth_setTargetTorque(slave, 0);           // 力矩归零
            lastCmdTorqueNm_[slave] = 0.0;
            break;
        default:
            break;
        }
    }
    out.limitExceeded = (std::fabs(posDeg) > travelLimitDeg_);   // 越界即提示（不限于被拦）
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

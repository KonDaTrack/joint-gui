#include "device/sim/JointModel.h"
#include <QDateTime>
#include <algorithm>

void JointModel::setCycleMs(int ms) { cycleMs_ = ms > 0 ? ms : 1; }
void JointModel::reset(double startDeg) { posDeg_ = startDeg; velDps_ = 0; torqueNm_ = 0; tempC_ = 25; }
void JointModel::enable() { if (!fault_) enabled_ = true; }
void JointModel::disable() { enabled_ = false; velDps_ = 0; torqueNm_ = 0; }
void JointModel::quickStop() { enabled_ = false; velDps_ = 0; torqueNm_ = 0; }
void JointModel::faultReset() { fault_ = false; errorCode_ = 0; enabled_ = false; velDps_ = 0; }
void JointModel::injectFault() { fault_ = true; enabled_ = false; errorCode_ = 0x1000; }
void JointModel::clearFault() { fault_ = false; errorCode_ = 0; }
void JointModel::setTarget(const Joint::TargetCommand& cmd) { target_ = cmd; }

Joint::Telemetry JointModel::step()
{
    if (fault_) {
        velDps_ = 0; torqueNm_ = 0;
        Joint::Telemetry t;
        t.slave = 1; t.connected = true;
        t.positionDeg = posDeg_; t.velocityDps = 0; t.torqueNm = 0;
        t.temperatureC = tempC_;
        t.statusWord = 0x0008;
        t.driveState = Joint::DriveState::Fault;
        t.errorCode = errorCode_;
        t.operateMode = mode_;
        t.timestampMs = QDateTime::currentMSecsSinceEpoch();
        return t;
    }

    if (enabled_) {
        double targetVel = 0.0;
        if (target_.hasPosition) {
            double err = target_.positionDeg - posDeg_;
            double maxV = target_.hasVelocity && target_.velocityDps > 0
                              ? target_.velocityDps : 30.0;
            targetVel = std::clamp(err, -maxV, maxV);   // 位置 P 控制 + 限速
        } else if (target_.hasVelocity) {
            targetVel = target_.velocityDps;
        }
        velDps_ += (targetVel - velDps_) * 0.2;          // 一阶逼近
        posDeg_ += velDps_ * cycleMs_ / 1000.0;
        torqueNm_ = (targetVel - velDps_) * 0.5 + target_.torqueNm;
        tempC_ += (torqueNm_ * torqueNm_ * 0.01) - (tempC_ - 25.0) * 0.001;
    } else {
        velDps_ = 0; torqueNm_ = 0;
    }

    Joint::Telemetry t;
    t.slave = 1; t.connected = true;
    t.positionDeg = posDeg_; t.velocityDps = velDps_; t.torqueNm = torqueNm_;
    t.temperatureC = tempC_;
    t.statusWord = enabled_ ? 0x0027 : 0x0021;
    t.driveState = Joint::mapDriveState(t.statusWord);
    t.errorCode = 0;
    t.operateMode = mode_;
    t.timestampMs = QDateTime::currentMSecsSinceEpoch();
    return t;
}

#pragma once
#include "device/JointTypes.h"

// 教学用简化关节模型：位置=速度积分，速度一阶逼近目标，可注入故障。
class JointModel
{
public:
    void setCycleMs(int ms);
    void reset(double startDeg);

    void enable();
    void disable();
    void quickStop();
    void faultReset();
    void injectFault();
    void clearFault();

    void setTarget(const Joint::TargetCommand& cmd);
    Joint::Telemetry step();

    bool enabled() const { return enabled_; }
    bool faulted() const { return fault_; }

private:
    int cycleMs_ = 10;
    double posDeg_ = 0.0;
    double velDps_ = 0.0;
    double torqueNm_ = 0.0;
    double tempC_ = 25.0;
    bool enabled_ = false;
    bool fault_ = false;
    quint16 errorCode_ = 0;
    Joint::TargetCommand target_;
    Joint::OperateMode mode_ = Joint::OperateMode::TorquePositionFixed;
};

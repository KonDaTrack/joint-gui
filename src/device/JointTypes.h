#pragma once
#include <QMetaType>
#include <QString>
#include <cstdint>

namespace Joint {

// Auto：启动时自动检测 EtherCAT → CANopen → 仿真 回退
enum class BusType { Auto, EtherCat, CanOpen, Simulation };

// 与两套 SDK 的 eth_OperateMode / canopen_OperateMode 数值一致，可 static_cast 互转
enum class OperateMode {
    Reserve = 0,
    ProfilePosition = 1,
    Velocity = 2,
    ProfileVelocity = 3,
    ProfileTorque = 4,
    Homing = 6,
    InterpolatedPosition = 7,
    CyclicSyncPosition = 8,
    CyclicSyncVelocity = 9,
    CyclicSyncTorque = 10,
    TorquePositionFixed = 11,
    Invalid = -1
};

enum class DriveState {
    NotReadyToSwitchOn,
    SwitchOnDisabled,
    ReadyToSwitchOn,
    SwitchedOn,
    OperationEnabled,
    QuickStopActive,
    FaultReactionActive,
    Fault,
    Unknown
};

struct Telemetry {
    quint16 slave = 0;
    bool connected = false;
    double positionDeg = 0.0;    // 当前角度 (deg)
    double velocityDps = 0.0;    // (deg/s)
    double torqueNm = 0.0;       // (N·m)
    double temperatureC = 0.0;
    quint16 statusWord = 0;
    DriveState driveState = DriveState::Unknown;
    quint16 errorCode = 0;
    OperateMode operateMode = OperateMode::Invalid;
    qint64 timestampMs = 0;
};

struct TargetCommand {
    double positionDeg = 0.0;
    double velocityDps = 0.0;
    double torqueNm = 0.0;
    bool hasPosition = false;
    bool hasVelocity = false;
    bool hasTorque = false;
    double profileVelocity = 0.0;      // 轮廓速度 (deg/s)
    double profileAcceleration = 0.0;  // 轮廓加速度 (deg/s^2)
    double profileDeceleration = 0.0;  // 轮廓减速度 (deg/s^2)
    double torqueSlopeNmPerSec = 0.0;  // 轮廓力矩斜率 (N·m/s，PT 模式)
    double kp = 0.0;                   // MIT 模式
    double kd = 0.0;
};

// 从站参数（自动读取；字段 0 = 未读到，valid() 三参数齐全才算有效）
struct DeviceParams {
    double encoderPulsesPerRev = 0;  // 编码器分辨率（脉冲/圈）
    double gearRatio = 0;            // 减速比（电机转数/输出轴转数）
    double ratedTorqueNm = 0;        // 额定扭矩 (N·m)
    bool valid() const {
        return encoderPulsesPerRev > 0 && gearRatio > 0 && ratedTorqueNm > 0;
    }
};

QString busTypeName(BusType t);

// CiA402 状态字 → DriveState
DriveState mapDriveState(quint16 statusWord);

} // namespace Joint

Q_DECLARE_METATYPE(Joint::Telemetry)
Q_DECLARE_METATYPE(Joint::TargetCommand)
Q_DECLARE_METATYPE(Joint::OperateMode)

#pragma once
#include "device/JointTypes.h"
#include <QString>

struct AppConfig {
    Joint::BusType busType = Joint::BusType::Auto;   // 默认自动检测
    quint16 slaveId = 1;

    // EtherCAT
    QString ethInterface;       // 网卡名，如 enx00e0bc4915ec
    int ethCycleMs = 2;         // 周期 1~5ms

    // CANopen
    int canBaudrateKbps = 1000;

    // 单位换算（EtherCAT 必需；CANopen/仿真可忽略）
    double encoderPulsesPerRev = 65536;  // 编码器分辨率
    double gearRatio = 1.0;              // 减速比
    double ratedTorqueNm = 1.0;          // 额定力矩 N·m

    int controlCycleMs() const;          // 工作周期
};

Q_DECLARE_METATYPE(AppConfig)

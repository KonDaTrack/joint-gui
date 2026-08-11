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
    double encoderPulsesPerRev = 524288;  // 编码器分辨率（本关节 19 位 = 524288）
    double gearRatio = 101.0;             // 减速比（本关节 101:1）
    double ratedTorqueNm = 28.0;          // 额定力矩 N·m（官方上位机实测 ±28；0x6076 读数单位不可靠）

    int controlCycleMs() const;          // 工作周期
};

Q_DECLARE_METATYPE(AppConfig)

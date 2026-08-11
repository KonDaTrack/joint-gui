#pragma once
#include "device/JointDevice.h"
#include <QHash>

class EthercatDevice : public JointDevice
{
public:
    ~EthercatDevice() override;

    bool open(const AppConfig& cfg) override;
    void close() override;
    int slaveCount() const override { return slaveCount_; }
    QList<quint16> slaveList() const override;

    bool enable(quint16 slave) override;
    bool disable(quint16 slave) override;
    bool faultReset(quint16 slave) override;
    bool quickStop(quint16 slave) override;
    bool setOperateMode(quint16 slave, Joint::OperateMode mode) override;
    bool setTarget(quint16 slave, const Joint::TargetCommand& cmd) override;
    bool readTelemetry(quint16 slave, Joint::Telemetry& out) override;
    bool homing(quint16 slave) override;

    bool readSDO(quint16 slave, quint16 index, quint8 subIndex,
                 void* value, int dataType, int timeout) override;
    bool writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                  const void* value, int dataType, int timeout) override;

private:
    bool inited_ = false;   // eth_initDLL 成功即置位，close 时必须 eth_freeDLL 释放网卡
    int slaveCount_ = 0;
    int cycleMs_ = 2;
    quint16 slaveId_ = 0;
    double pulsesPerRev_ = 65536;
    double gearRatio_ = 1.0;
    double ratedNm_ = 1.0;
    Joint::OperateMode mode_ = Joint::OperateMode::ProfilePosition;
    QHash<quint16, Joint::DeviceParams> paramsBySlave_;   // 每从站自动读取的参数
    // 速度由位置差分计算（0x606C 速度寄存器在静止时读数不稳）
    QHash<quint16, double> lastPosPulses_;
    QHash<quint16, qint64> lastPosTimeMs_;
    QHash<quint16, double> lastVelDps_;
    void readDeviceParams();
    Joint::DeviceParams paramsFor(quint16 slave) const;   // 有效参数或 cfg 回退
};

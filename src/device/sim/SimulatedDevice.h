#pragma once
#include "device/JointDevice.h"
#include "device/sim/JointModel.h"

// 无硬件仿真实现：包装 JointModel，完整走 JointDevice 接口。
class SimulatedDevice : public JointDevice
{
public:
    bool open(const AppConfig& cfg) override;
    void close() override;
    int slaveCount() const override { return 1; }

    bool enable(quint16 slave) override { Q_UNUSED(slave); model_.enable(); return true; }
    bool disable(quint16 slave) override { Q_UNUSED(slave); model_.disable(); return true; }
    bool faultReset(quint16 slave) override { Q_UNUSED(slave); model_.faultReset(); return true; }
    bool quickStop(quint16 slave) override { Q_UNUSED(slave); model_.quickStop(); return true; }
    bool setOperateMode(quint16 slave, Joint::OperateMode mode) override;
    bool setTarget(quint16 slave, const Joint::TargetCommand& cmd) override;
    bool readTelemetry(quint16 slave, Joint::Telemetry& out) override;

    bool readSDO(quint16 slave, quint16 index, quint8 subIndex,
                 void* value, int dataType, int timeout) override;
    bool writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                  const void* value, int dataType, int timeout) override;

private:
    JointModel model_;
};

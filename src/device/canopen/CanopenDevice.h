#pragma once
#include "device/JointDevice.h"
#include "device/canopen/MitFrameCodec.h"
#include <chrono>

class CanopenDevice : public JointDevice
{
public:
    ~CanopenDevice() override;

    bool open(const AppConfig& cfg) override;
    void close() override;
    int slaveCount() const override { return ready_ ? 1 : 0; }

    bool enable(quint16 slave) override;
    bool disable(quint16 slave) override;
    bool faultReset(quint16 slave) override;
    bool quickStop(quint16 slave) override;
    bool setOperateMode(quint16 slave, Joint::OperateMode mode) override;
    bool setTarget(quint16 slave, const Joint::TargetCommand& cmd) override;
    bool readTelemetry(quint16 slave, Joint::Telemetry& out) override;

    bool readSDO(quint16 slave, quint16 index, quint8 subIndex,
                 void* value, int dataType, int timeout) override;
    bool writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                  const void* value, int dataType, int timeout) override;

private:
    bool readMitLimits(quint16 slave);

    MitFrameCodec codec_;
    bool ready_ = false;
};

#pragma once
#include "device/JointTypes.h"
#include "core/AppConfig.h"

class JointDevice
{
public:
    virtual ~JointDevice() = default;

    virtual bool open(const AppConfig& cfg) = 0;
    virtual void close() = 0;
    virtual int slaveCount() const = 0;

    virtual bool enable(quint16 slave) = 0;
    virtual bool disable(quint16 slave) = 0;
    virtual bool faultReset(quint16 slave) = 0;
    virtual bool quickStop(quint16 slave) = 0;
    virtual bool setOperateMode(quint16 slave, Joint::OperateMode mode) = 0;
    virtual bool setTarget(quint16 slave, const Joint::TargetCommand& cmd) = 0;
    virtual bool readTelemetry(quint16 slave, Joint::Telemetry& out) = 0;

    // SDO/OD 访问（预留；dt 为 0x02~0x09 数据类型码）
    virtual bool readSDO(quint16 slave, quint16 index, quint8 subIndex,
                         void* value, int dataType, int timeout) = 0;
    virtual bool writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                          const void* value, int dataType, int timeout) = 0;
};

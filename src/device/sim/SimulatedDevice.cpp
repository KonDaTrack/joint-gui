#include "device/sim/SimulatedDevice.h"

bool SimulatedDevice::open(const AppConfig& cfg)
{
    model_.reset(0.0);
    model_.setCycleMs(cfg.controlCycleMs());
    return true;
}

void SimulatedDevice::close()
{
    model_.disable();
}

bool SimulatedDevice::setOperateMode(quint16 slave, Joint::OperateMode mode)
{
    Q_UNUSED(slave);
    return true;   // 仿真模型操作模式固定为 TorquePositionFixed
}

bool SimulatedDevice::setTarget(quint16 slave, const Joint::TargetCommand& cmd)
{
    Q_UNUSED(slave);
    model_.setTarget(cmd);
    return true;
}

bool SimulatedDevice::readTelemetry(quint16 slave, Joint::Telemetry& out)
{
    out = model_.step();
    out.slave = slave;
    return true;
}

bool SimulatedDevice::readSDO(quint16 slave, quint16 index, quint8 subIndex,
                              void* value, int dataType, int timeout)
{
    Q_UNUSED(slave); Q_UNUSED(index); Q_UNUSED(subIndex);
    Q_UNUSED(value); Q_UNUSED(dataType); Q_UNUSED(timeout);
    return false;
}

bool SimulatedDevice::writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                               const void* value, int dataType, int timeout)
{
    Q_UNUSED(slave); Q_UNUSED(index); Q_UNUSED(subIndex);
    Q_UNUSED(value); Q_UNUSED(dataType); Q_UNUSED(timeout);
    return false;
}

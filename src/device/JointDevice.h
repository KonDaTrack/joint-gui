#pragma once
#include <QList>
#include "device/JointTypes.h"
#include "core/AppConfig.h"

class JointDevice
{
public:
    virtual ~JointDevice() = default;

    virtual bool open(const AppConfig& cfg) = 0;
    virtual void close() = 0;
    virtual int slaveCount() const = 0;

    // 返回实际从站地址列表（EtherCAT: 1..N；CANopen: 探测到的在线节点 ID；仿真: {1}）
    virtual QList<quint16> slaveList() const = 0;

    virtual bool enable(quint16 slave) = 0;
    virtual bool disable(quint16 slave) = 0;
    virtual bool faultReset(quint16 slave) = 0;
    virtual bool quickStop(quint16 slave) = 0;
    virtual bool setOperateMode(quint16 slave, Joint::OperateMode mode) = 0;
    virtual bool setTarget(quint16 slave, const Joint::TargetCommand& cmd) = 0;
    virtual bool readTelemetry(quint16 slave, Joint::Telemetry& out) = 0;
    // 归航/归零（默认不支持；EtherCAT 实现）
    virtual bool homing(quint16) { return false; }
    // 回0：把关节运动到零点（默认不支持；EtherCAT 实现）
    virtual bool moveToZero(quint16) { return false; }
    // 识别出的从站型号（含额定力矩；未识别时说明"用手填值"）。空 = 不支持
    virtual QString modelInfo(quint16) const { return QString(); }
    // 型号短名（如 "70mm"），用于标签页/下拉。空 = 未识别
    virtual QString modelShortName(quint16) const { return QString(); }

    // SDO/OD 访问（预留；dt 为 0x02~0x09 数据类型码）
    virtual bool readSDO(quint16 slave, quint16 index, quint8 subIndex,
                         void* value, int dataType, int timeout) = 0;
    virtual bool writeSDO(quint16 slave, quint16 index, quint8 subIndex,
                          const void* value, int dataType, int timeout) = 0;
};

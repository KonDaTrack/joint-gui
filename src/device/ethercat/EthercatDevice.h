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
    bool moveToZero(quint16 slave) override;   // 回0：切 PP 模式走到 0 度

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
    double travelLimitDeg_ = 170.0;   // 行程限位 ±deg（相对归零零点）
    Joint::OperateMode mode_ = Joint::OperateMode::ProfilePosition;
    QHash<quint16, Joint::DeviceParams> paramsBySlave_;   // 每从站自动读取的参数
    // 速度由位置差分计算（0x606C 速度寄存器在静止时读数不稳）
    QHash<quint16, double> lastPosPulses_;
    QHash<quint16, qint64> lastPosTimeMs_;
    QHash<quint16, double> lastVelDps_;
    // 最近下发的速度/力矩，用于限位时判断运动方向（只拦"继续向外"，放行"反向回来"）
    QHash<quint16, double> lastCmdVelDps_;
    QHash<quint16, double> lastCmdTorqueNm_;
    void readDeviceParams();
    bool limitBlocksMotion(quint16 slave, double posDeg) const;   // 超限且朝外运动才拦
    Joint::DeviceParams paramsFor(quint16 slave) const;   // 有效参数或 cfg 回退
};

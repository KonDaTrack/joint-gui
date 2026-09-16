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
    QString modelInfo(quint16 slave) const override;
    QString modelShortName(quint16 slave) const override;

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
    // 操作模式按从站存：多关节时各轴可跑各的模式（全局一个值会互相覆盖）
    QHash<quint16, Joint::OperateMode> modeBySlave_;
    QHash<quint16, Joint::DeviceParams> paramsBySlave_;   // 每从站识别/读取的参数
    // 控制字 0x6040 在 RxPDO 输出缓冲里的字节偏移（连接时读 0x1600 映射表算出）。
    // -1 = 未在 PDO 里 / 读取失败 → 退回 eth_setControlWord。
    // 为什么要直接写缓冲：0x6040 被映射进 RxPDO，主站每周期都发 PDO，
    // 用 SDO 写会被立刻覆盖（实测使能失效）；而 eth_setControlWord 虽然写的是
    // PDO，却会在状态不迁移时阻塞 6 秒。直接写缓冲两头都避开。
    QHash<quint16, int> cwOffsetBySlave_;
    QHash<quint16, QString> modelNameBySlave_;            // 识别出的型号名（或"未识别(…)"）
    QHash<quint16, QString> modelShortBySlave_;           // 型号短名（70mm/90mm/110mm）
    QHash<quint16, bool> modelUnknownBySlave_;            // 型号未识别 → 用对话框手填值
    // 速度由位置差分计算（0x606C 速度寄存器在静止时读数不稳）。
    // 保留一小段位置历史，用 ~20ms 基线做差分：2ms 基线会把位置读数本身
    // 的 ±若干脉冲量化抖动放大成极大的速度噪声（实测静止时速度曲线满屏毛刺）。
    struct PosSample { qint64 ms; double pulses; };
    QHash<quint16, QList<PosSample>> posHist_;
    QHash<quint16, double> lastVelDps_;
    // 最近下发的速度/力矩，用于限位时判断运动方向（只拦"继续向外"，放行"反向回来"）
    QHash<quint16, double> lastCmdVelDps_;
    QHash<quint16, double> lastCmdTorqueNm_;
    void readDeviceParams();
    Joint::OperateMode modeFor(quint16 slave) const;              // 该从站的操作模式
    void writeControlWord(quint16 slave, quint16 word);           // 写 0x6040（走 PDO 缓冲，不阻塞）
    bool limitBlocksMotion(quint16 slave, double posDeg) const;   // 超限且朝外运动才拦
    Joint::DeviceParams paramsFor(quint16 slave) const;   // 有效参数或 cfg 回退
};

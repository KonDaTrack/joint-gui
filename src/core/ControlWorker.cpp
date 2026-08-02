#include "core/ControlWorker.h"
#include "device/DeviceFactory.h"
#include <QDateTime>
#include <QNetworkInterface>

ControlWorker::ControlWorker(QObject* parent)
    : QObject(parent)
{
    // 关键：定时器必须作为子对象随 moveToThread 一起迁移到工作线程，
    // 否则它停留在创建线程（UI 线程），start/stop 变成跨线程调用且周期退化。
    cycleTimer_.setParent(this);
    connect(&cycleTimer_, &QTimer::timeout, this, &ControlWorker::onCycle);
}

ControlWorker::~ControlWorker()
{
    disconnectDevice();
}

void ControlWorker::connectDevice(const AppConfig& cfg)
{
    disconnectDevice();
    cfg_ = cfg;

    if (cfg.busType == Joint::BusType::Auto) {
        detectAndConnect();
        return;
    }
    if (tryOpen(cfg)) return;
    emit connectionChanged(false, Joint::busTypeName(cfg.busType), 0,
                           QStringLiteral("连接失败"));
}

// 尝试打开候选设备；成功则接管 device_ 并启动周期循环，返回 true
bool ControlWorker::tryOpen(const AppConfig& c)
{
    device_ = createDevice(c.busType);
    if (!device_ || !device_->open(c)) {
        if (device_) device_->close();
        device_.reset();
        return false;
    }
    cfg_ = c;
    connected_ = true;
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    cycleTimer_.setInterval(cfg_.controlCycleMs());
    cycleTimer_.start();
    emit connectionChanged(true, Joint::busTypeName(cfg_.busType),
                           device_->slaveCount(), QString());
    return true;
}

// Auto 检测顺序：EtherCAT（遍历活动非回环网卡）→ CANopen → 仿真
void ControlWorker::detectAndConnect()
{
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifaces) {
        const QNetworkInterface::InterfaceFlags f = iface.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) ||
            !f.testFlag(QNetworkInterface::IsRunning) ||
            f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        emit detectionMessage(QStringLiteral("检测 EtherCAT 网卡 %1 ...").arg(iface.name()));
        AppConfig c = cfg_;
        c.busType = Joint::BusType::EtherCat;
        c.ethInterface = iface.name();
        if (tryOpen(c)) return;   // 失败时 device_ 已释放，继续下一个网卡
    }

    emit detectionMessage(QStringLiteral("未检测到 EtherCAT 从站，尝试 CANopen ..."));
    AppConfig c = cfg_;
    c.busType = Joint::BusType::CanOpen;
    if (tryOpen(c)) return;

    emit detectionMessage(QStringLiteral("未检测到 CANopen 设备，使用仿真模式"));
    AppConfig s = cfg_;
    s.busType = Joint::BusType::Simulation;
    if (!tryOpen(s)) {
        emit connectionChanged(false, QString(), 0, QStringLiteral("自动检测失败"));
    }
}

void ControlWorker::disconnectDevice()
{
    cycleTimer_.stop();
    if (device_) {
        device_->close();
        device_.reset();
    }
    connected_ = false;
}

void ControlWorker::onCycle()
{
    if (!device_ || !connected_) return;

    Joint::Telemetry t;
    if (device_->readTelemetry(cfg_.slaveId, t)) {
        lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
        emit telemetryUpdated(t);
        return;
    }

    // 读失败/超时看门狗：连续失败超 500ms 判定连接异常
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastTelemetryMs_ && (now - lastTelemetryMs_) > 500) {
        connected_ = false;
        cycleTimer_.stop();
        device_->close();
        device_.reset();   // 释放设备，避免重连时对已关闭设备二次 close
        emit faultDetected(QStringLiteral("遥测超时，连接已断开"));
        emit connectionChanged(false, QString(), 0, QStringLiteral("遥测超时"));
    }
}

void ControlWorker::enableRequested()
{
    if (device_) device_->enable(cfg_.slaveId);
}
void ControlWorker::disableRequested()
{
    if (device_) device_->disable(cfg_.slaveId);
}
void ControlWorker::quickStopRequested()
{
    if (device_) device_->quickStop(cfg_.slaveId);
}
void ControlWorker::faultResetRequested()
{
    if (device_) device_->faultReset(cfg_.slaveId);
}
void ControlWorker::setOperateModeRequested(Joint::OperateMode mode)
{
    if (device_) device_->setOperateMode(cfg_.slaveId, mode);
}
void ControlWorker::setTargetRequested(const Joint::TargetCommand& cmd)
{
    if (device_) device_->setTarget(cfg_.slaveId, cmd);
}

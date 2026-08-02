#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include "device/JointDevice.h"
#include "core/AppConfig.h"

// 运行在独立 QThread。SDK 调用全部发生在此线程；与 UI 通过跨线程队列信号交互。
class ControlWorker : public QObject
{
    Q_OBJECT
public:
    explicit ControlWorker(QObject* parent = nullptr);
    ~ControlWorker() override;

public slots:
    void connectDevice(const AppConfig& cfg);
    void disconnectDevice();
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void setOperateModeRequested(Joint::OperateMode mode);
    void setTargetRequested(const Joint::TargetCommand& cmd);

signals:
    void connectionChanged(bool connected, QString busName, int slaveCount, QString error);
    void telemetryUpdated(const Joint::Telemetry& telemetry);
    void faultDetected(QString message);
    void detectionMessage(QString message);   // 自动检测过程提示（显示在状态栏）

private slots:
    void onCycle();

private:
    bool tryOpen(const AppConfig& c);          // 尝试打开候选设备，成功则接管并启动周期
    void detectAndConnect();                   // Auto：EtherCAT → CANopen → 仿真

    std::unique_ptr<JointDevice> device_;
    QTimer cycleTimer_;
    AppConfig cfg_;
    bool connected_ = false;
    qint64 lastTelemetryMs_ = 0;
};

#pragma once
#include <QMainWindow>
#include <QThread>
#include "core/ControlWorker.h"

class MonitorPanel;
class ControlPanel;
class CurvePanel;
class ControlServer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void promptConnection();
    void onConnectionChanged(bool connected, QString busName, int slaveCount, QString error);
    void onFaultDetected(QString message);

private:
    QThread thread_;        // 设备线程（SDK 调用都在这里）
    QThread serverThread_;  // 网络线程（与设备/UI 隔离，服务端问题不得影响控制）
    ControlWorker* worker_ = nullptr;
    ControlServer* server_ = nullptr;
    MonitorPanel* monitor_ = nullptr;
    ControlPanel* control_ = nullptr;
    CurvePanel* curve_ = nullptr;
};

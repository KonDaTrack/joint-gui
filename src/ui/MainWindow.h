#pragma once
#include <QMainWindow>
#include <QThread>
#include "core/ControlWorker.h"

class MonitorPanel;
class ControlPanel;
class CurvePanel;

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
    QThread thread_;
    ControlWorker* worker_;
    MonitorPanel* monitor_;
    ControlPanel* control_;
    CurvePanel* curve_;
};

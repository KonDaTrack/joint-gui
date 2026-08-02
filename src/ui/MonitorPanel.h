#pragma once
#include <QWidget>
#include <QLabel>
#include "device/JointTypes.h"

// 实时监控面板：位置/速度/力矩/温度/状态字/驱动状态/故障码/刷新率。
class MonitorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MonitorPanel(QWidget* parent = nullptr);

public slots:
    void onTelemetry(const Joint::Telemetry& t);

private:
    QLabel* value(const QString& name);
    QLabel* pos_, *vel_, *tor_, *temp_, *status_, *state_, *err_, *conn_, *freq_;
    int samples_ = 0;
    qint64 lastFreqMs_ = 0;
    double freqHz_ = 0.0;  // 实测刷新率（freq_ 已被 QLabel 占用）
};

#pragma once
#include <QWidget>
#include <QLabel>
#include <QHash>
#include <QTabWidget>
#include "device/JointTypes.h"

// 实时监控面板：每从站一个标签页，显示位置/速度/力矩/温度/状态字/驱动状态/故障码/连接/刷新率。
class MonitorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MonitorPanel(QWidget* parent = nullptr);

public slots:
    void onTelemetry(const QList<Joint::Telemetry>& list);
    void setSlaves(const QList<quint16>& slaves);

private:
    struct Page {
        QWidget* page = nullptr;
        QLabel *pos = nullptr, *vel = nullptr, *tor = nullptr, *temp = nullptr,
               *status = nullptr, *state = nullptr, *err = nullptr, *conn = nullptr, *freq = nullptr;
        int samples = 0;
        qint64 lastFreqMs = 0;
        double freqHz = 0.0;
    };
    QLabel* value();
    Page makePage(quint16 slave);
    void updatePage(Page& p, const Joint::Telemetry& t);

    QTabWidget* tabs_;
    QHash<quint16, Page> pages_;
};

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

signals:
    // 用户点击标签页切换从站（程序化同步时不发，避免联动回环）
    void activeSlaveChanged(quint16 address);

public slots:
    void onTelemetry(const QList<Joint::Telemetry>& list);
    void setSlaves(const QList<quint16>& slaves);
    void setActiveSlave(quint16 address);                 // 程序化同步（静默）
    void setSlaveModels(const QStringList& shortNames, const QStringList& modelInfos);

private:
    struct Page {
        QWidget* page = nullptr;
        QLabel *model = nullptr;   // 识别出的型号（供核对参数是否配对）
        QLabel *pos = nullptr, *vel = nullptr, *tor = nullptr, *temp = nullptr,
               *status = nullptr, *state = nullptr, *err = nullptr, *conn = nullptr, *freq = nullptr;
        int samples = 0;
        qint64 lastFreqMs = 0;
        double freqHz = 0.0;
    };
    QLabel* value(const char* objectName = nullptr);
    QWidget* withUnit(QLabel* plate, const QString& unit);   // 数值框 + 独立单位小字
    Page makePage(quint16 slave);
    void updatePage(Page& p, const Joint::Telemetry& t);

    QTabWidget* tabs_;
    QHash<quint16, Page> pages_;
    QList<quint16> order_;   // 从站顺序，用于按 active 切换标签页
    QStringList shorts_;     // 各从站型号短名，构造标签页标题用
    bool syncing_ = false;   // 程序化切换标签页时置位，抑制 activeSlaveChanged
};

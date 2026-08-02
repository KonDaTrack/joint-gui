#pragma once
#include <QWidget>
#include <QVector>
#include "device/JointTypes.h"

// 自绘滚动实时曲线：位置/速度/力矩三条轨迹。
class CurvePanel : public QWidget
{
    Q_OBJECT
public:
    explicit CurvePanel(QWidget* parent = nullptr);
    void setBufferSize(int n);

public slots:
    void onTelemetry(const QList<Joint::Telemetry>& list);
    void setActiveSlave(quint16 address);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void push(QVector<double>& buf, double v);
    void drawTrace(QPainter& p, const QVector<double>& buf, const QColor& c, int yPad);

    QVector<double> pos_, vel_, tor_;
    quint16 activeSlave_ = 1;
    int bufferSize_ = 300;
};

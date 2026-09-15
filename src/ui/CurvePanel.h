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
    // 采集控制：平时不记录不显示；点「下发目标」后清空并开始记录（观察本次响应）
    void beginCapture();
    void stopCapture();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    // 一条轨迹：数据 + 显示量程状态。
    // minSpan 是关键：信号平稳时不把微小抖动放大到满屏（否则噪声看着像剧烈震荡）。
    // center/span 做平滑跟随，避免每帧重新缩放导致波形整体跳动。
    struct Trace {
        QVector<double> buf;
        QColor color;
        double minSpan = 1.0;        // 最小显示量程（该轨迹的单位）
        double center = 0.0;         // 平滑后的显示中心
        double span = 0.0;           // 平滑后的显示量程
        bool scaled = false;         // 是否已初始化量程
    };

    void push(Trace& tr, double v);
    void drawTrace(QPainter& p, Trace& tr, int yPad);

    Trace pos_, vel_, tor_;
    quint16 activeSlave_ = 1;
    int bufferSize_ = 300;
    bool recording_ = false;   // 仅在「下发目标」后为真；否则不采样也不绘制
};

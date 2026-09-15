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
    int bufferSize_ = 5000;    // 10s @500Hz：一次完整运动通常几秒，300 点(0.6s)装不下
    bool recording_ = false;   // 仅在「下发目标」后为真；否则不采样也不绘制
    // 力矩显示平滑：驱动器基于电流估算的力矩有几十‰的高频噪声（实测使能静止时
    // ±1.4 N·m 摆动而位置纹丝不动），直接画是一条毛刺带。这里只平滑「显示值」，
    // 不改变遥测原始数据（监控面板的数字仍是原始值）。
    double torSmooth_ = 0.0;
    bool torSmoothInit_ = false;
    bool moved_ = false;       // 本次记录中是否真的动过（用于判定"运动完成"）
    qint64 stillSinceMs_ = 0;  // 连续静止的起点时间戳（0=当前不静止）
    qint64 firstMs_ = 0, lastMs_ = 0;   // 本次记录的时间跨度（标题显示）
};

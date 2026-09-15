#include "ui/CurvePanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

CurvePanel::CurvePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PanelCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    pos_.color = QColor(0x4f, 0xc3, 0xf7);   // 蓝 位置
    vel_.color = QColor(0x2e, 0xcc, 0x71);   // 绿 速度
    tor_.color = QColor(0xf3, 0x9c, 0x12);   // 橙 力矩
    // 最小显示量程：按关节的典型量级设，信号小于它不再放大，
    // 否则静止时的微小抖动会被拉伸到满屏，看着像剧烈震荡
    pos_.minSpan = 1.0;    // deg
    vel_.minSpan = 10.0;   // deg/s
    tor_.minSpan = 1.0;    // N·m
}

void CurvePanel::setBufferSize(int n)
{
    bufferSize_ = qMax(2, n);
    pos_.buf.clear(); vel_.buf.clear(); tor_.buf.clear();
}

void CurvePanel::push(Trace& tr, double v)
{
    tr.buf.append(v);
    if (tr.buf.size() > bufferSize_) tr.buf.remove(0, tr.buf.size() - bufferSize_);
}

// 点「下发目标」：清空并开始记录，用来看本次指令的响应
void CurvePanel::beginCapture()
{
    pos_.buf.clear(); vel_.buf.clear(); tor_.buf.clear();
    pos_.scaled = vel_.scaled = tor_.scaled = false;   // 重新建立显示量程
    recording_ = true;
    update();
}

// 停止记录（停止运动/失能/急停）：保留已记录的波形，只是不再采样
void CurvePanel::stopCapture()
{
    recording_ = false;
    update();
}

void CurvePanel::onTelemetry(const QList<Joint::Telemetry>& list)
{
    if (!recording_) return;   // 未在下发目标后的观察窗口内，不采样
    for (const Joint::Telemetry& t : list) {
        if (!t.connected) continue;   // 断开条目不下发，避免把故障画成归零冲断曲线
        if (t.slave == activeSlave_) {
            // 力矩最小量程按额定力矩取 10%：驱动基于电流估算的力矩本身有 ~1% 额定的
            // 纹波（实测 110mm 在 -2~+9‰，即 0.9 N·m，而此时轴纹丝不动）。
            // 若用固定量程，小关节够用、大关节（84 N·m）会被这点纹波占满整个面板。
            if (t.ratedTorqueNm > 0.0)
                tor_.minSpan = t.ratedTorqueNm * 0.1;
            push(pos_, t.positionDeg);
            push(vel_, t.velocityDps);
            push(tor_, t.torqueNm);
            update();
            return;
        }
    }
}

void CurvePanel::setActiveSlave(quint16 address)
{
    activeSlave_ = address;
    pos_.buf.clear(); vel_.buf.clear(); tor_.buf.clear();
    pos_.scaled = vel_.scaled = tor_.scaled = false;   // 换轴重新建立显示量程
    update();
}

void CurvePanel::drawTrace(QPainter& p, Trace& tr, int yPad)
{
    const QVector<double>& buf = tr.buf;
    if (buf.isEmpty()) return;

    // 每条轨迹独立自动缩放（位置/速度/力矩量级差异大，共用缩放会压平小信号）
    double lo = buf[0], hi = buf[0];
    for (const auto& v : buf) { lo = qMin(lo, v); hi = qMax(hi, v); }

    // 目标量程：至少 minSpan，另加 10% 余量避免贴边
    const double targetSpan = qMax(tr.minSpan, (hi - lo) * 1.2);
    const double targetCenter = (lo + hi) * 0.5;

    // 平滑跟随目标量程：直接每帧重算会让波形随噪声整体跳动，
    // 平滑后小幅抖动不再引起缩放变化（配合 minSpan，静止时曲线基本是一条直线）
    const double k = 0.15;
    if (!tr.scaled) {
        tr.center = targetCenter;
        tr.span = targetSpan;
        tr.scaled = true;
    } else {
        tr.center += (targetCenter - tr.center) * k;
        tr.span   += (targetSpan - tr.span) * k;
    }
    const double loDisp = tr.center - tr.span * 0.5;
    const double range = qMax(1e-9, tr.span);

    p.setPen(QPen(tr.color, 1.5));
    QPainterPath path;
    for (int i = 0; i < buf.size(); ++i) {
        const double x = (double)i / (bufferSize_ - 1) * (width() - 2 * yPad) + yPad;
        const double y = height() - yPad - (buf[i] - loDisp) / range * (height() - 2 * yPad);
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    p.drawPath(path);
}

void CurvePanel::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    QPainter p(this);
    // 内部绘图区略深，与外层卡片（#242830 边框）形成层次；留 1px 让 QSS 边框可见
    p.fillRect(rect().adjusted(1, 1, -1, -1), QColor(0x1A, 0x1D, 0x21));
    p.setPen(QColor(0x3A, 0x40, 0x46));
    for (int i = 1; i < 4; ++i) {
        const int y = height() * i / 4;
        p.drawLine(1, y, width() - 1, y);
    }

    p.setRenderHint(QPainter::Antialiasing);
    const int pad = 10;
    drawTrace(p, pos_, pad);   // 蓝 位置
    drawTrace(p, vel_, pad);   // 绿 速度
    drawTrace(p, tor_, pad);   // 橙 力矩

    if (!recording_ && pos_.buf.isEmpty() && vel_.buf.isEmpty() && tor_.buf.isEmpty()) {
        // 空闲未记录：不显示曲线，只提示怎么开始
        p.setPen(QColor(0x7A, 0x83, 0x8C));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("点「下发目标」后开始记录波形"));
        return;
    }

    p.setPen(QColor(0xD0, 0xD6, 0xDD));
    p.drawText(10, 18, recording_
               ? tr("● 记录中 —— 位置(蓝) 速度(绿) 力矩(橙)")
               : tr("已停止记录 —— 位置(蓝) 速度(绿) 力矩(橙)"));
}

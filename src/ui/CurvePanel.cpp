#include "ui/CurvePanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

CurvePanel::CurvePanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CurvePanel::setBufferSize(int n)
{
    bufferSize_ = qMax(2, n);
    pos_.clear(); vel_.clear(); tor_.clear();
}

void CurvePanel::push(QVector<double>& buf, double v)
{
    buf.append(v);
    if (buf.size() > bufferSize_) buf.remove(0, buf.size() - bufferSize_);
}

void CurvePanel::onTelemetry(const Joint::Telemetry& t)
{
    push(pos_, t.positionDeg);
    push(vel_, t.velocityDps);
    push(tor_, t.torqueNm);
    update();
}

void CurvePanel::drawTrace(QPainter& p, const QVector<double>& buf, const QColor& c,
                           double min, double max, int yPad)
{
    if (buf.isEmpty()) return;
    const double range = qMax(1e-6, max - min);
    p.setPen(QPen(c, 1.5));
    QPainterPath path;
    for (int i = 0; i < buf.size(); ++i) {
        const double x = (double)i / (bufferSize_ - 1) * (width() - 2 * yPad) + yPad;
        const double y = height() - yPad - (buf[i] - min) / range * (height() - 2 * yPad);
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    p.drawPath(path);
}

void CurvePanel::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x20, 0x20));
    p.setPen(QColor(0x50, 0x50, 0x50));
    for (int i = 1; i < 4; ++i) {
        const int y = height() * i / 4;
        p.drawLine(0, y, width(), y);
    }

    double min = 0, max = 1;
    for (const auto& v : pos_) { min = qMin(min, v); max = qMax(max, v); }
    for (const auto& v : vel_) { min = qMin(min, v); max = qMax(max, v); }
    for (const auto& v : tor_) { min = qMin(min, v); max = qMax(max, v); }

    const int pad = 8;
    drawTrace(p, pos_, QColor(0x4f, 0xc3, 0xf7), min, max, pad);   // 蓝 位置
    drawTrace(p, vel_, QColor(0x2e, 0xcc, 0x71), min, max, pad);   // 绿 速度
    drawTrace(p, tor_, QColor(0xf3, 0x9c, 0x12), min, max, pad);   // 橙 力矩

    p.setPen(Qt::white);
    p.drawText(8, 16, tr("位置(蓝) 速度(绿) 力矩(橙)"));
}

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

void CurvePanel::onTelemetry(const QList<Joint::Telemetry>& list)
{
    for (const Joint::Telemetry& t : list) {
        if (!t.connected) continue;   // 断开条目不下发，避免把故障画成归零冲断曲线
        if (t.slave == activeSlave_) {
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
    pos_.clear(); vel_.clear(); tor_.clear();
    update();
}

void CurvePanel::drawTrace(QPainter& p, const QVector<double>& buf, const QColor& c,
                           int yPad)
{
    if (buf.isEmpty()) return;
    // 每条轨迹独立自动缩放（量级差异大的位置/速度/力矩共用缩放会压平小信号）
    double lo = buf[0], hi = buf[0];
    for (const auto& v : buf) { lo = qMin(lo, v); hi = qMax(hi, v); }
    const double span = qMax(1e-6, hi - lo);
    lo -= span * 0.1;   // ±10% 余量，避免贴边
    hi += span * 0.1;
    const double range = hi - lo;   // 加余量后恒 > 0，不能再 clamp（否则空闲轨迹贴底）

    p.setPen(QPen(c, 1.5));
    QPainterPath path;
    for (int i = 0; i < buf.size(); ++i) {
        const double x = (double)i / (bufferSize_ - 1) * (width() - 2 * yPad) + yPad;
        const double y = height() - yPad - (buf[i] - lo) / range * (height() - 2 * yPad);
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
    drawTrace(p, pos_, QColor(0x4f, 0xc3, 0xf7), pad);   // 蓝 位置
    drawTrace(p, vel_, QColor(0x2e, 0xcc, 0x71), pad);   // 绿 速度
    drawTrace(p, tor_, QColor(0xf3, 0x9c, 0x12), pad);   // 橙 力矩

    p.setPen(QColor(0xD0, 0xD6, 0xDD));
    p.drawText(10, 18, tr("位置(蓝) 速度(绿) 力矩(橙)"));
}

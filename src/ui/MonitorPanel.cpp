#include "ui/MonitorPanel.h"
#include <QDateTime>
#include <QFormLayout>
#include <QVBoxLayout>

QLabel* MonitorPanel::value()
{
    QLabel* lab = new QLabel(QStringLiteral("--"), this);
    lab->setMinimumWidth(160);
    return lab;
}

MonitorPanel::MonitorPanel(QWidget* parent)
    : QWidget(parent)
{
    tabs_ = new QTabWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(tabs_);
}

MonitorPanel::Page MonitorPanel::makePage(quint16 slave)
{
    Page p;
    QWidget* w = new QWidget(tabs_);
    QFormLayout* form = new QFormLayout(w);
    p.pos    = value();  form->addRow(tr("位置 (deg)"), p.pos);
    p.vel    = value();  form->addRow(tr("速度 (deg/s)"), p.vel);
    p.tor    = value();  form->addRow(tr("力矩 (N·m)"), p.tor);
    p.temp   = value();  form->addRow(tr("驱动器温度 (°C)"), p.temp);
    p.status = value();  form->addRow(tr("状态字 (hex)"), p.status);
    p.state  = value();  form->addRow(tr("驱动状态"), p.state);
    p.err    = value();  form->addRow(tr("故障码"), p.err);
    p.conn   = value();  form->addRow(tr("连接状态"), p.conn);
    p.freq   = value();  form->addRow(tr("刷新率"), p.freq);
    p.page = w;
    Q_UNUSED(slave);
    return p;
}

void MonitorPanel::setSlaves(const QList<quint16>& slaves)
{
    while (tabs_->count() > 0) {
        QWidget* w = tabs_->widget(0);
        tabs_->removeTab(0);
        delete w;   // 页内 QLabel 随页释放
    }
    pages_.clear();
    order_ = slaves;
    for (quint16 s : slaves) {
        Page p = makePage(s);
        pages_.insert(s, p);
        tabs_->addTab(p.page, QStringLiteral("从站 %1").arg(s));
    }
    if (slaves.isEmpty()) {
        tabs_->addTab(new QLabel(QStringLiteral("未连接"), tabs_), QStringLiteral("--"));
    }
}

void MonitorPanel::setActiveSlave(quint16 address)
{
    const int idx = order_.indexOf(address);
    if (idx >= 0) tabs_->setCurrentIndex(idx);
}

void MonitorPanel::onTelemetry(const QList<Joint::Telemetry>& list)
{
    for (const Joint::Telemetry& t : list) {
        auto it = pages_.find(t.slave);
        if (it != pages_.end()) updatePage(*it, t);
    }
}

void MonitorPanel::updatePage(Page& p, const Joint::Telemetry& t)
{
    p.pos->setText(QString::number(t.positionDeg, 'f', 2));
    p.vel->setText(QString::number(t.velocityDps, 'f', 2));
    p.tor->setText(QString::number(t.torqueNm, 'f', 3));
    p.temp->setText(t.temperatureC > 0 ? QString::number(t.temperatureC, 'f', 1)
                                       : QStringLiteral("N/A"));
    p.status->setText(QStringLiteral("0x%1").arg(t.statusWord, 4, 16, QLatin1Char('0')));

    const char* stateStr = "未知";
    switch (t.driveState) {
    case Joint::DriveState::NotReadyToSwitchOn: stateStr = "未就绪"; break;
    case Joint::DriveState::SwitchOnDisabled:   stateStr = "禁止合闸"; break;
    case Joint::DriveState::ReadyToSwitchOn:    stateStr = "待合闸"; break;
    case Joint::DriveState::SwitchedOn:         stateStr = "已合闸"; break;
    case Joint::DriveState::OperationEnabled:   stateStr = "运行使能"; break;
    case Joint::DriveState::QuickStopActive:    stateStr = "快速停机"; break;
    case Joint::DriveState::FaultReactionActive: stateStr = "故障反应"; break;
    case Joint::DriveState::Fault:              stateStr = "故障"; break;
    case Joint::DriveState::Unknown: break;
    }
    p.state->setText(QString::fromUtf8(stateStr));
    p.err->setText(t.errorCode ? QStringLiteral("0x%1").arg(t.errorCode, 4, 16, QLatin1Char('0'))
                               : QStringLiteral("无"));
    p.conn->setText(t.connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    p.conn->setStyleSheet(t.connected ? QStringLiteral("color: green;")
                                      : QStringLiteral("color: red;"));

    ++p.samples;
    if (p.lastFreqMs == 0) p.lastFreqMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (p.samples >= 20) {
        p.freqHz = 1000.0 * p.samples / (now - p.lastFreqMs);
        p.samples = 0;
        p.lastFreqMs = now;
        p.freq->setText(QStringLiteral("%1 Hz").arg(p.freqHz, 0, 'f', 1));
    }
}

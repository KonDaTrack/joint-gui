#include "ui/MonitorPanel.h"
#include <QDateTime>
#include <QFormLayout>
#include <QVBoxLayout>

QLabel* MonitorPanel::value(const QString& name)
{
    QLabel* lab = new QLabel(QStringLiteral("--"), this);
    lab->setMinimumWidth(160);
    return lab;
}

MonitorPanel::MonitorPanel(QWidget* parent)
    : QWidget(parent)
{
    pos_   = value(tr("位置"));
    vel_   = value(tr("速度"));
    tor_   = value(tr("力矩"));
    temp_  = value(tr("温度"));
    status_= value(tr("状态字"));
    state_ = value(tr("驱动状态"));
    err_   = value(tr("故障码"));
    conn_  = value(tr("连接"));
    freq_  = value(tr("刷新率"));

    QFormLayout* form = new QFormLayout;
    form->addRow(tr("位置 (deg)"), pos_);
    form->addRow(tr("速度 (deg/s)"), vel_);
    form->addRow(tr("力矩 (N·m)"), tor_);
    form->addRow(tr("驱动器温度 (°C)"), temp_);
    form->addRow(tr("状态字 (hex)"), status_);
    form->addRow(tr("驱动状态"), state_);
    form->addRow(tr("故障码"), err_);
    form->addRow(tr("连接状态"), conn_);
    form->addRow(tr("刷新率"), freq_);

    setLayout(form);
}

void MonitorPanel::onTelemetry(const Joint::Telemetry& t)
{
    pos_->setText(QString::number(t.positionDeg, 'f', 2));
    vel_->setText(QString::number(t.velocityDps, 'f', 2));
    tor_->setText(QString::number(t.torqueNm, 'f', 3));
    temp_->setText(t.temperatureC > 0 ? QString::number(t.temperatureC, 'f', 1)
                                      : QStringLiteral("N/A"));
    status_->setText(QStringLiteral("0x%1").arg(t.statusWord, 4, 16, QLatin1Char('0')));

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
    state_->setText(QString::fromUtf8(stateStr));
    err_->setText(t.errorCode ? QStringLiteral("0x%1").arg(t.errorCode, 4, 16, QLatin1Char('0'))
                              : QStringLiteral("无"));
    conn_->setText(t.connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    conn_->setStyleSheet(t.connected ? QStringLiteral("color: green;")
                                     : QStringLiteral("color: red;"));

    ++samples_;
    if (lastFreqMs_ == 0) lastFreqMs_ = QDateTime::currentMSecsSinceEpoch();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (samples_ >= 20) {
        freqHz_ = 1000.0 * samples_ / (now - lastFreqMs_);
        samples_ = 0;
        lastFreqMs_ = now;
        freq_->setText(QStringLiteral("%1 Hz").arg(freqHz_, 0, 'f', 1));
    }
}

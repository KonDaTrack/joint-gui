#include "ui/ControlPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ControlPanel::ControlPanel(QWidget* parent)
    : QWidget(parent)
{
    // 控制从站下拉：连接前禁用（占位 "--" data 0），检测到从站后由 setSlaves 填充
    slaveCombo_ = new QComboBox(this);
    slaveCombo_->addItem(QStringLiteral("--"), 0);
    slaveCombo_->setEnabled(false);
    connect(slaveCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                emit activeSlaveChanged(static_cast<quint16>(slaveCombo_->currentData().toInt()));
            });

    QHBoxLayout* slaveRow = new QHBoxLayout;
    slaveRow->addWidget(new QLabel(tr("控制从站"), this));
    slaveRow->addWidget(slaveCombo_, 1);

    // 急停按钮：最显眼
    estopBtn_ = new QPushButton(QStringLiteral("急停 ESTOP"), this);
    estopBtn_->setStyleSheet(QStringLiteral(
        "background:#c0392b;color:white;font-size:20px;font-weight:bold;"
        "min-height:56px;border-radius:8px;"));
    connect(estopBtn_, &QPushButton::clicked, this, &ControlPanel::onEstopClicked);

    readyCheck_ = new QCheckBox(QStringLiteral("已确认现场安全"), this);
    enableBtn_ = new QPushButton(QStringLiteral("使能"), this);
    enableBtn_->setEnabled(false);
    connect(readyCheck_, &QCheckBox::toggled, enableBtn_, &QPushButton::setEnabled);
    connect(enableBtn_, &QPushButton::clicked, this, &ControlPanel::onEnableClicked);

    disableBtn_ = new QPushButton(QStringLiteral("失能"), this);
    connect(disableBtn_, &QPushButton::clicked, this, &ControlPanel::disableRequested);

    faultResetBtn_ = new QPushButton(QStringLiteral("故障复位"), this);
    connect(faultResetBtn_, &QPushButton::clicked, this, &ControlPanel::onFaultResetClicked);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QStringLiteral("同步位置 CSP"), (int)Joint::OperateMode::CyclicSyncPosition);
    modeCombo_->addItem(QStringLiteral("同步速度 CSV"), (int)Joint::OperateMode::CyclicSyncVelocity);
    modeCombo_->addItem(QStringLiteral("同步力矩 CST"), (int)Joint::OperateMode::CyclicSyncTorque);
    modeCombo_->addItem(QStringLiteral("力矩位置混合"), (int)Joint::OperateMode::TorquePositionFixed);

    posEdit_   = new QLineEdit(QStringLiteral("0"), this);
    velEdit_   = new QLineEdit(QStringLiteral("0"), this);
    torEdit_   = new QLineEdit(QStringLiteral("0"), this);
    profVelEdit_ = new QLineEdit(QStringLiteral("10"), this);
    profAccEdit_ = new QLineEdit(QStringLiteral("50"), this);
    profDecEdit_ = new QLineEdit(QStringLiteral("50"), this);
    kpEdit_    = new QLineEdit(QStringLiteral("5"), this);
    kdEdit_    = new QLineEdit(QStringLiteral("2"), this);

    sendBtn_ = new QPushButton(QStringLiteral("下发目标"), this);
    connect(sendBtn_, &QPushButton::clicked, this, &ControlPanel::onSendTarget);

    stopBtn_ = new QPushButton(QStringLiteral("停止运动"), this);
    connect(stopBtn_, &QPushButton::clicked, this, &ControlPanel::onStopMotion);

    QHBoxLayout* estopRow = new QHBoxLayout;
    estopRow->addWidget(estopBtn_, 2);
    estopRow->addWidget(readyCheck_, 1);

    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->addWidget(enableBtn_);
    btnRow->addWidget(disableBtn_);
    btnRow->addWidget(faultResetBtn_);

    QFormLayout* form = new QFormLayout;
    form->addRow(tr("操作模式"), modeCombo_);
    form->addRow(tr("目标位置 (deg)"), posEdit_);
    form->addRow(tr("目标速度 (deg/s)"), velEdit_);
    form->addRow(tr("目标力矩 (N·m)"), torEdit_);
    form->addRow(tr("轮廓速度 (deg/s)"), profVelEdit_);
    form->addRow(tr("轮廓加速度 (deg/s²)"), profAccEdit_);
    form->addRow(tr("轮廓减速度 (deg/s²)"), profDecEdit_);
    form->addRow(tr("MIT KP"), kpEdit_);
    form->addRow(tr("MIT KD"), kdEdit_);

    QHBoxLayout* targetRow = new QHBoxLayout;
    targetRow->addWidget(sendBtn_);
    targetRow->addWidget(stopBtn_);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->addLayout(slaveRow);
    root->addLayout(estopRow);
    root->addLayout(btnRow);
    root->addWidget(new QLabel(tr("目标设定"), this));
    root->addLayout(form);
    root->addLayout(targetRow);
    root->addStretch();
}

Joint::OperateMode ControlPanel::currentMode() const
{
    return static_cast<Joint::OperateMode>(modeCombo_->currentData().toInt());
}

void ControlPanel::setBusType(Joint::BusType type)
{
    if (type == Joint::BusType::CanOpen) {
        const int idx = modeCombo_->findData((int)Joint::OperateMode::TorquePositionFixed);
        if (idx >= 0) modeCombo_->setCurrentIndex(idx);
        modeCombo_->setEnabled(false);
    } else {
        modeCombo_->setEnabled(true);
    }
}

void ControlPanel::setSlaves(const QList<quint16>& slaves)
{
    const quint16 cur = static_cast<quint16>(slaveCombo_->currentData().toInt());
    QSignalBlocker b(slaveCombo_);   // 重建期间抑制信号，避免误发 activeSlaveChanged 覆盖 worker 的 active
    slaveCombo_->clear();
    for (quint16 s : slaves)
        slaveCombo_->addItem(QStringLiteral("从站 %1").arg(s), s);
    slaveCombo_->setEnabled(!slaves.isEmpty());
    if (slaveCombo_->findData(cur) < 0 && slaveCombo_->count() > 0)
        slaveCombo_->setCurrentIndex(0);
}

void ControlPanel::setActiveSlave(quint16 address)
{
    const int idx = slaveCombo_->findData(address);
    if (idx >= 0) {
        QSignalBlocker b(slaveCombo_);
        slaveCombo_->setCurrentIndex(idx);
    }
}

void ControlPanel::onEnableClicked()
{
    emit operateModeChanged(currentMode());
    emit enableRequested();
}

void ControlPanel::onEstopClicked()
{
    emit quickStopRequested();
    emit disableRequested();
    // 急停后复位安全确认勾选，再次使能必须重新确认现场安全
    readyCheck_->setChecked(false);
}

void ControlPanel::onFaultResetClicked()
{
    if (QMessageBox::question(this, tr("故障复位"),
            tr("确定要复位故障吗？"), QMessageBox::Yes | QMessageBox::No)
            == QMessageBox::Yes) {
        emit faultResetRequested();
    }
}

void ControlPanel::onSendTarget()
{
    Joint::TargetCommand c;
    const Joint::OperateMode m = currentMode();
    switch (m) {
    case Joint::OperateMode::CyclicSyncPosition:
    case Joint::OperateMode::ProfilePosition:
        c.hasPosition = true;
        c.positionDeg = posEdit_->text().toDouble();
        c.profileVelocity = profVelEdit_->text().toDouble();
        c.profileAcceleration = profAccEdit_->text().toDouble();
        c.profileDeceleration = profDecEdit_->text().toDouble();
        break;
    case Joint::OperateMode::CyclicSyncVelocity:
    case Joint::OperateMode::ProfileVelocity:
    case Joint::OperateMode::Velocity:
        c.hasVelocity = true;
        c.velocityDps = velEdit_->text().toDouble();
        break;
    case Joint::OperateMode::CyclicSyncTorque:
    case Joint::OperateMode::ProfileTorque:
        c.hasTorque = true;
        c.torqueNm = torEdit_->text().toDouble();
        break;
    case Joint::OperateMode::TorquePositionFixed:
        c.hasPosition = true;
        c.positionDeg = posEdit_->text().toDouble();
        c.velocityDps = velEdit_->text().toDouble();
        c.torqueNm = torEdit_->text().toDouble();
        c.kp = kpEdit_->text().toDouble();
        c.kd = kdEdit_->text().toDouble();
        break;
    default:
        break;
    }
    emit targetRequested(c);
}

void ControlPanel::onStopMotion()
{
    Joint::TargetCommand c;
    c.hasVelocity = true;
    c.velocityDps = 0.0;
    emit targetRequested(c);
}

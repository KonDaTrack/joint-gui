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
    setObjectName(QStringLiteral("PanelCard"));
    setAttribute(Qt::WA_StyledBackground, true);

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

    // 急停按钮：最显眼（全局 QSS #dangerButton 红色醒目样式）
    estopBtn_ = new QPushButton(QStringLiteral("急停 ESTOP"), this);
    estopBtn_->setObjectName(QStringLiteral("dangerButton"));
    connect(estopBtn_, &QPushButton::clicked, this, &ControlPanel::onEstopClicked);

    readyCheck_ = new QCheckBox(QStringLiteral("已确认现场安全"), this);
    enableBtn_ = new QPushButton(QStringLiteral("使能"), this);
    enableBtn_->setObjectName(QStringLiteral("primaryButton"));
    enableBtn_->setEnabled(false);
    connect(readyCheck_, &QCheckBox::toggled, enableBtn_, &QPushButton::setEnabled);
    connect(enableBtn_, &QPushButton::clicked, this, &ControlPanel::onEnableClicked);

    disableBtn_ = new QPushButton(QStringLiteral("失能"), this);
    connect(disableBtn_, &QPushButton::clicked, this, &ControlPanel::disableRequested);

    faultResetBtn_ = new QPushButton(QStringLiteral("故障复位"), this);
    faultResetBtn_->setObjectName(QStringLiteral("warningButton"));
    connect(faultResetBtn_, &QPushButton::clicked, this, &ControlPanel::onFaultResetClicked);

    homeBtn_ = new QPushButton(QStringLiteral("归零"), this);
    homeBtn_->setEnabled(false);
    connect(homeBtn_, &QPushButton::clicked, this, &ControlPanel::homingRequested);
    connect(readyCheck_, &QCheckBox::toggled, homeBtn_, &QPushButton::setEnabled);

    modeCombo_ = new QComboBox(this);
    // 轮廓模式：驱动内部生成平滑轨迹，主站一发目标即可（对齐官方 PP/PV/PT 例程）
    modeCombo_->addItem(QStringLiteral("轮廓位置 PP"), (int)Joint::OperateMode::ProfilePosition);
    modeCombo_->addItem(QStringLiteral("轮廓速度 PV"), (int)Joint::OperateMode::ProfileVelocity);
    modeCombo_->addItem(QStringLiteral("轮廓力矩 PT"), (int)Joint::OperateMode::ProfileTorque);

    posEdit_   = new QLineEdit(QStringLiteral("0"), this);
    velEdit_   = new QLineEdit(QStringLiteral("0"), this);
    torEdit_   = new QLineEdit(QStringLiteral("0"), this);
    profVelEdit_ = new QLineEdit(QStringLiteral("10"), this);
    profAccEdit_ = new QLineEdit(QStringLiteral("10"), this);
    profDecEdit_ = new QLineEdit(QStringLiteral("10"), this);
    torSlopeEdit_ = new QLineEdit(QStringLiteral("10"), this);

    sendBtn_ = new QPushButton(QStringLiteral("下发目标"), this);
    sendBtn_->setObjectName(QStringLiteral("primaryButton"));
    connect(sendBtn_, &QPushButton::clicked, this, &ControlPanel::onSendTarget);

    stopBtn_ = new QPushButton(QStringLiteral("停止运动"), this);
    stopBtn_->setObjectName(QStringLiteral("warningButton"));
    connect(stopBtn_, &QPushButton::clicked, this, &ControlPanel::onStopMotion);

    QHBoxLayout* estopRow = new QHBoxLayout;
    estopRow->addWidget(estopBtn_, 2);
    estopRow->addWidget(readyCheck_, 1);

    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->addWidget(enableBtn_);
    btnRow->addWidget(disableBtn_);
    btnRow->addWidget(faultResetBtn_);
    btnRow->addWidget(homeBtn_);

    form_ = new QFormLayout;
    form_->addRow(tr("操作模式"), modeCombo_);
    form_->addRow(tr("目标位置 (deg)"), posEdit_);
    form_->addRow(tr("目标速度 (deg/s)"), velEdit_);
    form_->addRow(tr("目标力矩 (N·m)"), torEdit_);
    form_->addRow(tr("轮廓速度 (deg/s)"), profVelEdit_);
    form_->addRow(tr("轮廓加速度 (deg/s²)"), profAccEdit_);
    form_->addRow(tr("轮廓减速度 (deg/s²)"), profDecEdit_);
    form_->addRow(tr("力矩斜率 (N·m/s)"), torSlopeEdit_);
    connect(modeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ControlPanel::updateFieldVisibility);
    updateFieldVisibility();

    QHBoxLayout* targetRow = new QHBoxLayout;
    targetRow->addWidget(sendBtn_);
    targetRow->addWidget(stopBtn_);

    QLabel* targetTitle = new QLabel(tr("目标设定"), this);
    targetTitle->setObjectName(QStringLiteral("sectionTitle"));

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);
    root->addLayout(slaveRow);
    root->addLayout(estopRow);
    root->addLayout(btnRow);
    root->addWidget(targetTitle);
    root->addLayout(form_);
    root->addLayout(targetRow);
    root->addStretch();
}

Joint::OperateMode ControlPanel::currentMode() const
{
    return static_cast<Joint::OperateMode>(modeCombo_->currentData().toInt());
}

void ControlPanel::setBusType(Joint::BusType type)
{
    // CANopen 暂未适配 PP/PV/PT，本次仅 EtherCAT 使用；保留接口便于后续扩展
    Q_UNUSED(type);
    modeCombo_->setEnabled(true);
}

// 按操作模式只显示相关字段：PP→位置+轮廓；PV→速度+轮廓加/减；PT→力矩+斜率
void ControlPanel::updateFieldVisibility()
{
    const Joint::OperateMode m = currentMode();
    const bool posMode = (m == Joint::OperateMode::ProfilePosition
                          || m == Joint::OperateMode::InterpolatedPosition);
    const bool velMode = (m == Joint::OperateMode::ProfileVelocity
                          || m == Joint::OperateMode::Velocity);
    const bool torMode = (m == Joint::OperateMode::ProfileTorque);

    // 隐藏/显示目标字段（含标签）。本 Qt 无 QFormLayout::setRowVisible，用 labelForField 一并隐藏。
    auto vis = [this](QWidget* w, bool v) {
        if (!w) return;
        w->setVisible(v);
        if (form_) {
            QWidget* lab = form_->labelForField(w);
            if (lab) lab->setVisible(v);
        }
    };
    vis(posEdit_, posMode);
    vis(velEdit_, velMode);
    vis(torEdit_, torMode);
    vis(profVelEdit_, posMode);
    vis(profAccEdit_, posMode || velMode);
    vis(profDecEdit_, posMode || velMode);
    vis(torSlopeEdit_, torMode);
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
    case Joint::OperateMode::ProfilePosition:
    case Joint::OperateMode::InterpolatedPosition:
        c.hasPosition = true;
        c.positionDeg = posEdit_->text().toDouble();
        c.profileVelocity = profVelEdit_->text().toDouble();
        c.profileAcceleration = profAccEdit_->text().toDouble();
        c.profileDeceleration = profDecEdit_->text().toDouble();
        break;
    case Joint::OperateMode::ProfileVelocity:
    case Joint::OperateMode::Velocity:
        c.hasVelocity = true;
        c.velocityDps = velEdit_->text().toDouble();
        c.profileAcceleration = profAccEdit_->text().toDouble();
        c.profileDeceleration = profDecEdit_->text().toDouble();
        break;
    case Joint::OperateMode::ProfileTorque:
        c.hasTorque = true;
        c.torqueNm = torEdit_->text().toDouble();
        c.torqueSlopeNmPerSec = torSlopeEdit_->text().toDouble();
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

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

    // 控制目标：只显示，不提供切换控件（切换统一走左侧监控面板标签页，
    // 避免两个入口互相不同步）。显示里提示切换位置，方便操作者找到。
    targetLabel_ = new QLabel(this);
    targetLabel_->setStyleSheet(QStringLiteral("color: #38BDF8; font-weight: bold;"));
    refreshTargetLabel();

    // 控制权显示 + 本地开关。仲裁权在本机：上位机只能"请求"，本地可随时收回
    ownerLabel_ = new QLabel(this);
    remoteCheck_ = new QCheckBox(QStringLiteral("允许上位机接管控制"), this);
    remoteCheck_->setToolTip(QStringLiteral(
        "勾选后，上位机可以请求接管控制权（接管时全部轴会先失能）。\n"
        "取消勾选会立即收回控制权，并让所有轴失能。"));
    connect(remoteCheck_, &QCheckBox::toggled, this, &ControlPanel::remoteAllowedChanged);

    // 急停按钮：最显眼（全局 QSS #dangerButton 红色醒目样式）
    estopBtn_ = new QPushButton(QStringLiteral("急停 ESTOP"), this);
    estopBtn_->setObjectName(QStringLiteral("dangerButton"));
    connect(estopBtn_, &QPushButton::clicked, this, &ControlPanel::onEstopClicked);

    readyCheck_ = new QCheckBox(QStringLiteral("已确认现场安全"), this);
    enableBtn_ = new QPushButton(QStringLiteral("使能"), this);
    enableBtn_->setObjectName(QStringLiteral("primaryButton"));
    enableBtn_->setEnabled(false);
    connect(enableBtn_, &QPushButton::clicked, this, &ControlPanel::onEnableClicked);

    disableBtn_ = new QPushButton(QStringLiteral("失能"), this);
    connect(disableBtn_, &QPushButton::clicked, this, [this] {
        emit captureStopped();
        emit disableRequested();
    });

    faultResetBtn_ = new QPushButton(QStringLiteral("故障复位"), this);
    faultResetBtn_->setObjectName(QStringLiteral("warningButton"));
    connect(faultResetBtn_, &QPushButton::clicked, this, &ControlPanel::onFaultResetClicked);

    homeBtn_ = new QPushButton(QStringLiteral("归零"), this);
    homeBtn_->setEnabled(false);
    connect(homeBtn_, &QPushButton::clicked, this, &ControlPanel::homingRequested);


    zeroBtn_ = new QPushButton(QStringLiteral("回0"), this);
    zeroBtn_->setEnabled(false);
    connect(zeroBtn_, &QPushButton::clicked, this, &ControlPanel::moveToZeroRequested);
    connect(readyCheck_, &QCheckBox::toggled, this, &ControlPanel::updateCommandEnabled);

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
    stopBtn_->setObjectName(QStringLiteral("dangerActionButton"));   // 与「故障复位」区分：停止是危险动作
    connect(stopBtn_, &QPushButton::clicked, this, &ControlPanel::onStopMotion);

    QHBoxLayout* estopRow = new QHBoxLayout;
    estopRow->addWidget(estopBtn_, 2);
    // 垂直居中：急停按钮高 52px，复选框自然高度小，不显式指定会偏上贴边
    estopRow->addWidget(readyCheck_, 1, Qt::AlignVCenter);

    // 5 个按钮等分卡片宽度：用自然宽度会超出右栏可用宽度，最右侧的「回0」被裁掉半个字
    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    for (QPushButton* b : {enableBtn_, disableBtn_, faultResetBtn_, homeBtn_, zeroBtn_}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumWidth(0);
        btnRow->addWidget(b);
    }

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
    root->addWidget(targetLabel_);
    root->addWidget(ownerLabel_);
    root->addWidget(remoteCheck_);
    root->addLayout(estopRow);
    root->addLayout(btnRow);
    root->addWidget(targetTitle);
    root->addLayout(form_);
    root->addLayout(targetRow);
    root->addStretch();

    // 放在最后：此时所有控件都已创建，updateCommandEnabled() 才能安全访问它们
    setControlOwner(QStringLiteral("local"));
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
    slaves_ = slaves;
    if (!slaves_.contains(activeSlave_) && !slaves_.isEmpty())
        activeSlave_ = slaves_.first();   // 重连后原目标可能不存在了
    refreshTargetLabel();
}

// 组合「从站N · XXmm」文本；型号未知时退回「从站N」
QString ControlPanel::slaveText(quint16 address) const
{
    const int idx = address - 1;
    const QString sn = (idx >= 0 && idx < shorts_.size()) ? shorts_.at(idx) : QString();
    return sn.isEmpty() ? QStringLiteral("从站%1").arg(address)
                        : QStringLiteral("从站%1 · %2").arg(address).arg(sn);
}

// 刷新「控制目标」显示，并通知外部（状态栏提示）
void ControlPanel::refreshTargetLabel()
{
    if (!slaves_.contains(activeSlave_)) {
        targetLabel_->setText(QStringLiteral("▶ 当前控制目标：--"));
        return;
    }
    const QString text = slaveText(activeSlave_);
    // 文本保持短：这行没有换行，过长会把整栏最小宽度撑大，
    // 反过来挤压下面那排按钮（表现为「回0」被裁或右侧对不齐）。
    // 「在哪切换」的提示移到 tooltip，不再占用行宽。
    targetLabel_->setText(QStringLiteral("▶ 控制目标：%1").arg(text));
    targetLabel_->setToolTip(tr("命令发往该从站。切换：点击左侧监控面板的从站标签页"));
    emit controlTargetChanged(text);
}

void ControlPanel::setSlaveModels(const QStringList& shortNames)
{
    shorts_ = shortNames;
    refreshTargetLabel();   // 型号变了，显示跟着更新
}

// 命令类控件的可用性 = 本地持控制权 AND 已勾选安全确认。
// 使能/归零/回0 两个条件都要满足，所以两处变更都调这里，避免互相覆盖。
void ControlPanel::updateCommandEnabled()
{
    const bool local = (owner_ != QLatin1String("remote"));
    const bool ready = readyCheck_ && readyCheck_->isChecked();
    const bool en = local && ready;

    if (enableBtn_) enableBtn_->setEnabled(en);
    if (homeBtn_)   homeBtn_->setEnabled(en);
    if (zeroBtn_)   zeroBtn_->setEnabled(en);
    // 故障复位/模式/下发目标：只受控制权约束
    if (faultResetBtn_) faultResetBtn_->setEnabled(local);
    if (modeCombo_)     modeCombo_->setEnabled(local);
    if (sendBtn_)       sendBtn_->setEnabled(local);
    for (QLineEdit* e : {posEdit_, velEdit_, torEdit_, profVelEdit_,
                         profAccEdit_, profDecEdit_, torSlopeEdit_}) {
        if (e) e->setEnabled(local);
    }
    // 失能/停止运动/急停：安全类，任何时候都可用，不参与置灰
}

// 远程持有时禁用本地操作按钮。急停不在此列——安全命令任何时候都要能按。
void ControlPanel::setControlOwner(const QString& owner)
{
    owner_ = owner;
    const bool local = (owner != QLatin1String("remote"));
    if (ownerLabel_) {
        ownerLabel_->setText(local ? tr("▶ 控制权：本机（下位机）")
                                   : tr("▶ 控制权：上位机（远程）"));
        ownerLabel_->setStyleSheet(local
            ? QStringLiteral("color: #34D399; font-weight: bold;")
            : QStringLiteral("color: #F59E0B; font-weight: bold;"));
    }
    updateCommandEnabled();
}


void ControlPanel::setActiveSlave(quint16 address)
{
    activeSlave_ = address;
    refreshTargetLabel();
}

void ControlPanel::onEnableClicked()
{
    emit operateModeChanged(currentMode());
    emit enableRequested();
}

void ControlPanel::onEstopClicked()
{
    emit captureStopped();
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
    emit captureStarted();   // 每次下发目标都重新开始记录本次响应
}

void ControlPanel::onStopMotion()
{
    Joint::TargetCommand c;
    c.hasVelocity = true;
    c.velocityDps = 0.0;
    emit targetRequested(c);
    emit captureStopped();
}

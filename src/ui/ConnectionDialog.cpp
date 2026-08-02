#include "ui/ConnectionDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("连接关节模组"));
    setMinimumWidth(460);

    QLabel* title = new QLabel(QStringLiteral("连接关节模组总线"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));

    busCombo_ = new QComboBox(this);
    busCombo_->addItem(QStringLiteral("自动检测"), (int)Joint::BusType::Auto);
    busCombo_->addItem(QStringLiteral("仿真"), (int)Joint::BusType::Simulation);
    busCombo_->addItem(QStringLiteral("EtherCAT"), (int)Joint::BusType::EtherCat);
    busCombo_->addItem(QStringLiteral("CANopen"), (int)Joint::BusType::CanOpen);

    baudCombo_ = new QComboBox(this);
    baudCombo_->addItem(QStringLiteral("1000 kbps"), 1000);
    baudCombo_->addItem(QStringLiteral("500 kbps"), 500);
    baudCombo_->addItem(QStringLiteral("250 kbps"), 250);

    ifEdit_ = new QLineEdit(QStringLiteral("enx00e0bc4915ec"), this);
    slaveEdit_ = new QLineEdit(QStringLiteral("1"), this);
    cycleEdit_ = new QLineEdit(QStringLiteral("2"), this);
    pulsesEdit_ = new QLineEdit(QStringLiteral("65536"), this);
    gearEdit_ = new QLineEdit(QStringLiteral("1"), this);
    ratedTorqueEdit_ = new QLineEdit(QStringLiteral("1"), this);

    QFormLayout* form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->addRow(QStringLiteral("总线类型"), busCombo_);
    form->addRow(QStringLiteral("EtherCAT 网卡"), ifEdit_);
    form->addRow(QStringLiteral("从站 ID"), slaveEdit_);
    form->addRow(QStringLiteral("CAN 波特率"), baudCombo_);
    form->addRow(QStringLiteral("EtherCAT 周期 (ms)"), cycleEdit_);
    form->addRow(QStringLiteral("编码器分辨率 (脉冲/圈)"), pulsesEdit_);
    form->addRow(QStringLiteral("减速比"), gearEdit_);
    form->addRow(QStringLiteral("额定力矩 (N·m)"), ratedTorqueEdit_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setSpacing(14);
    root->addWidget(title);
    root->addLayout(form);
    root->addWidget(buttons_);

    onBusChanged();
    connect(busCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ConnectionDialog::onBusChanged);
}

void ConnectionDialog::onBusChanged()
{
    const Joint::BusType t = static_cast<Joint::BusType>(busCombo_->currentData().toInt());
    const bool eth = (t == Joint::BusType::EtherCat);
    const bool can = (t == Joint::BusType::CanOpen);
    // Auto：检测时自动决定，总线相关参数先禁用（网卡名由检测自动枚举）
    ifEdit_->setEnabled(eth);
    baudCombo_->setEnabled(can);
    cycleEdit_->setEnabled(eth);
}

AppConfig ConnectionDialog::config() const
{
    AppConfig c;
    c.busType = static_cast<Joint::BusType>(busCombo_->currentData().toInt());
    c.ethInterface = ifEdit_->text().trimmed();
    c.slaveId = static_cast<quint16>(slaveEdit_->text().toUInt());
    c.ethCycleMs = qMax(1, cycleEdit_->text().toInt());
    c.canBaudrateKbps = baudCombo_->currentData().toInt();
    c.encoderPulsesPerRev = qMax(1.0, pulsesEdit_->text().toDouble());
    c.gearRatio = qMax(0.0001, gearEdit_->text().toDouble());
    c.ratedTorqueNm = ratedTorqueEdit_->text().toDouble();
    return c;
}

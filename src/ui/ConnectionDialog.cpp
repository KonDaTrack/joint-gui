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

    // 关节型号：默认"自动检测"——连接后按驱动器 0x6076 逐从站识别型号，
    // 用该型号的规格值（多关节各轴独立）。这里的型号选择只是**识别失败时的兜底**，
    // 选定后自动填额定力矩（力矩换算的唯一标定参数，填错会超发/少发）。
    modelCombo_ = new QComboBox(this);
    modelCombo_->addItem(QStringLiteral("自动检测（按驱动器识别）"), 0.0);
    modelCombo_->addItem(QStringLiteral("PHU-14H-70-F-B（额定 9.6）"), 9.6);
    modelCombo_->addItem(QStringLiteral("PHU-20H-90-F-B（额定 50）"), 50.0);
    modelCombo_->addItem(QStringLiteral("PHU-25H-110-F-B（额定 84）"), 84.0);
    modelCombo_->addItem(QStringLiteral("自定义"), 0.0);

    ifEdit_ = new QLineEdit(QStringLiteral("enx00e0bc4915ec"), this);
    slaveEdit_ = new QLineEdit(QStringLiteral("1"), this);
    cycleEdit_ = new QLineEdit(QStringLiteral("2"), this);
    pulsesEdit_ = new QLineEdit(QStringLiteral("524288"), this);
    gearEdit_ = new QLineEdit(QStringLiteral("101"), this);
    ratedTorqueEdit_ = new QLineEdit(QStringLiteral("50"), this);
    ratedTorqueEdit_->setToolTip(QStringLiteral(
        "力矩换算基准。连接后按驱动器自动识别型号并以型号规格值为准；\n"
        "仅当识别失败时才用此手填值。\n"
        "注意：填得过小会导致力矩超发（危险），填得偏大只会欠发（安全）。"));
    travelLimitEdit_ = new QLineEdit(QStringLiteral("170"), this);

    QFormLayout* form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->addRow(QStringLiteral("总线类型"), busCombo_);
    form->addRow(QStringLiteral("关节型号"), modelCombo_);
    form->addRow(QStringLiteral("EtherCAT 网卡"), ifEdit_);
    form->addRow(QStringLiteral("从站 ID"), slaveEdit_);
    form->addRow(QStringLiteral("CAN 波特率"), baudCombo_);
    form->addRow(QStringLiteral("EtherCAT 周期 (ms)"), cycleEdit_);
    form->addRow(QStringLiteral("编码器分辨率 (脉冲/圈)"), pulsesEdit_);
    form->addRow(QStringLiteral("减速比"), gearEdit_);
    form->addRow(QStringLiteral("额定力矩 (N·m)"), ratedTorqueEdit_);
    form->addRow(QStringLiteral("行程限位 ±(deg)"), travelLimitEdit_);

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
    connect(modelCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ConnectionDialog::onModelChanged);
    onModelChanged();   // 按默认型号填好额定力矩
}

// 选型号只改「额定力矩」：它是力矩换算的唯一标定参数，且各型号差异达 5 倍，手填易错。
// 减速比不自动填——设计文档该列不可靠（实物 90mm 是 101，文档写 100），以铭牌为准。
void ConnectionDialog::onModelChanged()
{
    const double rated = modelCombo_->currentData().toDouble();
    if (rated > 0.0)
        ratedTorqueEdit_->setText(QString::number(rated, 'g', 4));
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
    c.travelLimitDeg = qBound(1.0, travelLimitEdit_->text().toDouble(), 360.0);
    return c;
}

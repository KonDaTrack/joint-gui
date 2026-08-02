#pragma once
#include <QDialog>
#include "core/AppConfig.h"

class QComboBox;
class QLineEdit;
class QDialogButtonBox;

// 启动连接对话框：选择总线类型并填写参数，输出 AppConfig。
class ConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget* parent = nullptr);
    AppConfig config() const;

private:
    void onBusChanged();

    QComboBox* busCombo_;
    QComboBox* baudCombo_;
    QLineEdit* ifEdit_;        // EtherCAT 网卡
    QLineEdit* slaveEdit_;
    QLineEdit* cycleEdit_;
    QLineEdit* pulsesEdit_;    // 编码器分辨率
    QLineEdit* gearEdit_;      // 减速比
    QLineEdit* ratedTorqueEdit_;
    QDialogButtonBox* buttons_;
};

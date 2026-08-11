#pragma once
#include <QWidget>
#include <QList>
#include "device/JointTypes.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QPushButton;

// 控制面板：急停/使能/失能/故障复位/模式/目标值。
// 使能需先勾选“已确认现场安全”。
class ControlPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPanel(QWidget* parent = nullptr);
    void setBusType(Joint::BusType type);
    void setSlaves(const QList<quint16>& slaves);
    void setActiveSlave(quint16 address);

signals:
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void operateModeChanged(Joint::OperateMode mode);
    void targetRequested(const Joint::TargetCommand& cmd);
    void activeSlaveChanged(quint16 address);

private slots:
    void onEnableClicked();
    void onEstopClicked();
    void onFaultResetClicked();
    void onSendTarget();
    void onStopMotion();
    void updateFieldVisibility();

private:
    Joint::OperateMode currentMode() const;

    QCheckBox* readyCheck_;
    QPushButton* estopBtn_;
    QPushButton* enableBtn_;
    QPushButton* disableBtn_;
    QPushButton* faultResetBtn_;
    QComboBox* slaveCombo_;
    QComboBox* modeCombo_;
    QLineEdit* posEdit_;
    QLineEdit* velEdit_;
    QLineEdit* torEdit_;
    QLineEdit* profVelEdit_;
    QLineEdit* profAccEdit_;
    QLineEdit* profDecEdit_;
    QLineEdit* torSlopeEdit_;
    QPushButton* sendBtn_;
    QPushButton* stopBtn_;
    QFormLayout* form_;   // 目标设定表单，用于按模式隐藏/显示字段
};

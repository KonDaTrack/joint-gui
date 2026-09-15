#pragma once
#include <QWidget>
#include <QList>
#include "device/JointTypes.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
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
    void setSlaveModels(const QStringList& shortNames);   // 下标 i ↔ 从站 i+1

signals:
    // 控制目标变化（切换从站后），供状态栏提示。参数是完整描述如「从站2 · 70mm」
    void controlTargetChanged(const QString& label);
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void operateModeChanged(Joint::OperateMode mode);
    void targetRequested(const Joint::TargetCommand& cmd);
    void activeSlaveChanged(quint16 address);
    void homingRequested();
    void moveToZeroRequested();

private slots:
    void onEnableClicked();
    void onEstopClicked();
    void onFaultResetClicked();
    void onSendTarget();
    void onStopMotion();
    void updateFieldVisibility();

private:
    Joint::OperateMode currentMode() const;
    void refreshTargetLabel();   // 按当前下拉项刷新「控制目标」标注

    QCheckBox* readyCheck_;
    QPushButton* estopBtn_;
    QPushButton* enableBtn_;
    QPushButton* disableBtn_;
    QPushButton* faultResetBtn_;
    QPushButton* homeBtn_;
    QPushButton* zeroBtn_;
    QLabel* targetLabel_;   // 醒目标注当前控制目标，避免命令发错轴
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

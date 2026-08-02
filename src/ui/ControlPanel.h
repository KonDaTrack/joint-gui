#pragma once
#include <QWidget>
#include "device/JointTypes.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

// 控制面板：急停/使能/失能/故障复位/模式/目标值。
// 使能需先勾选“已确认现场安全”。
class ControlPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPanel(QWidget* parent = nullptr);
    // CANopen 仅支持力矩位置混合，限制模式下拉避免下发零刚度 MIT 帧
    void setBusType(Joint::BusType type);

signals:
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void operateModeChanged(Joint::OperateMode mode);
    void targetRequested(const Joint::TargetCommand& cmd);

private slots:
    void onEnableClicked();
    void onEstopClicked();
    void onFaultResetClicked();
    void onSendTarget();
    void onStopMotion();

private:
    Joint::OperateMode currentMode() const;

    QCheckBox* readyCheck_;
    QPushButton* estopBtn_;
    QPushButton* enableBtn_;
    QPushButton* disableBtn_;
    QPushButton* faultResetBtn_;
    QComboBox* modeCombo_;
    QLineEdit* posEdit_;
    QLineEdit* velEdit_;
    QLineEdit* torEdit_;
    QLineEdit* profVelEdit_;
    QLineEdit* profAccEdit_;
    QLineEdit* profDecEdit_;
    QLineEdit* kpEdit_;
    QLineEdit* kdEdit_;
    QPushButton* sendBtn_;
    QPushButton* stopBtn_;
};

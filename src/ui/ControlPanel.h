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
    void setActiveSlave(quint16 address);                 // 由左侧标签页驱动
    void setSlaveModels(const QStringList& shortNames);   // 下标 i ↔ 从站 i+1

signals:
    // 控制目标变化，供状态栏提示。参数是完整描述如「从站2 · 70mm」
    void controlTargetChanged(const QString& label);
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void operateModeChanged(Joint::OperateMode mode);
    void targetRequested(const Joint::TargetCommand& cmd);
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
    QString slaveText(quint16 address) const;   // 「从站N · XXmm」
    void refreshTargetLabel();                  // 按当前从站刷新「控制目标」显示

    // 从站信息（本面板只显示，不提供切换；切换由左侧监控面板标签页驱动）
    QList<quint16> slaves_;
    QStringList shorts_;
    quint16 activeSlave_ = 0;

    QCheckBox* readyCheck_;
    QPushButton* estopBtn_;
    QPushButton* enableBtn_;
    QPushButton* disableBtn_;
    QPushButton* faultResetBtn_;
    QPushButton* homeBtn_;
    QPushButton* zeroBtn_;
    QLabel* targetLabel_;   // 醒目标注当前控制目标，避免命令发错轴
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

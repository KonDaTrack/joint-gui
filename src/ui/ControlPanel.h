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
    // owner: "local"（本机控制）或 "remote"（上位机控制）。
    // 远程持有时本地操作按钮置灰——不是"点了没反应"，要让人一眼看出为什么点不动
    void setControlOwner(const QString& owner);

signals:
    // 控制目标变化，供状态栏提示。参数是完整描述如「从站2 · 70mm」
    void controlTargetChanged(const QString& label);
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void operateModeChanged(Joint::OperateMode mode);
    void targetRequested(const Joint::TargetCommand& cmd);
    // 波形采集控制：下发目标→开始记录本次响应；停止/失能/急停→停止记录
    void captureStarted();
    void captureStopped();
    void homingRequested();
    void moveToZeroRequested();
    void remoteAllowedChanged(bool allowed);   // 本地是否允许上位机接管

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
    void updateCommandEnabled();                // 按控制权 + 安全确认刷新按钮可用性

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
    QLabel* ownerLabel_;    // 当前控制权归属（本地/上位机）
    QCheckBox* remoteCheck_;  // 本地开关：允许上位机接管
    QString owner_ = QStringLiteral("local");   // 当前控制权（"local"/"remote"）
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

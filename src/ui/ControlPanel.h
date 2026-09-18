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
    // 「停止运动」单独一条路：原来它和「下发目标」共用 targetRequested，
    // 而负载链挂在"下发目标"上——不拆开的话点停止会先写一次负载。
    void stopMotionRequested();
    // 波形采集的触发在 ControlWorker（targetCommanded/motionStopped）——
    // 放那儿才能同时覆盖本地与远程两条命令路径
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

    // 一律给 nullptr 初值：构造期间若有代码（如 setControlOwner）在控件创建前
    // 访问这些成员，未初始化的指针是随机值，会直接段错误
    QCheckBox* readyCheck_ = nullptr;
    QPushButton* estopBtn_ = nullptr;
    QPushButton* enableBtn_ = nullptr;
    QPushButton* disableBtn_ = nullptr;
    QPushButton* faultResetBtn_ = nullptr;
    QPushButton* homeBtn_ = nullptr;
    QPushButton* zeroBtn_ = nullptr;
    QLabel* targetLabel_ = nullptr;   // 醒目标注当前控制目标，避免命令发错轴
    QLabel* ownerLabel_ = nullptr;    // 当前控制权归属（本地/上位机）
    // 负载（磁粉制动器）的设置与状态在**监控面板**（刷新率下面），不在这里重复一份
    QCheckBox* remoteCheck_ = nullptr;  // 本地开关：允许上位机接管
    QString owner_ = QStringLiteral("local");   // 当前控制权（"local"/"remote"）
    QComboBox* modeCombo_ = nullptr;
    QLineEdit* posEdit_ = nullptr;
    QLineEdit* velEdit_ = nullptr;
    QLineEdit* torEdit_ = nullptr;
    QLineEdit* profVelEdit_ = nullptr;
    QLineEdit* profAccEdit_ = nullptr;
    QLineEdit* profDecEdit_ = nullptr;
    QLineEdit* torSlopeEdit_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QFormLayout* form_ = nullptr;   // 目标设定表单，用于按模式隐藏/显示字段
};

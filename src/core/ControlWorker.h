#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include <QJsonObject>
#include <memory>
#include "device/JointDevice.h"
#include "core/AppConfig.h"

// 控制权归属。上位机（远程）与下位机（本地）都可能发命令，
// 必须仲裁——否则两边同时往驱动器写目标，电机行为不可预测
// （与"官方串口工具和 EtherCAT 抢控制权"是同一个坑）。
enum class ControlOwner { Local, Remote };

// 运行在独立 QThread。SDK 调用全部发生在此线程；与 UI 通过跨线程队列信号交互。
class ControlWorker : public QObject
{
    Q_OBJECT
public:
    explicit ControlWorker(QObject* parent = nullptr);
    ~ControlWorker() override;

public slots:
    void connectDevice(const AppConfig& cfg);
    void disconnectDevice();
    void selectSlave(quint16 address);      // 切换控制从站
    void enableRequested();
    void disableRequested();
    void quickStopRequested();
    void faultResetRequested();
    void setOperateModeRequested(Joint::OperateMode mode);
    void setTargetRequested(const Joint::TargetCommand& cmd);
    void homingRequested();
    void moveToZeroRequested();

    // ---- 控制权 ----
    // 本地开关：是否允许远程（上位机）接管。关掉时若远程正持有，立即收回。
    void setRemoteAllowed(bool allowed);
    void requestRemoteControl();    // 远程客户端请求控制权
    void releaseRemoteControl();    // 远程客户端主动交还
    void remoteHeartbeat();         // 远程心跳（持有控制权时周期发送）
    // 远程命令统一入口。name: enable/disable/quickStop/faultReset/setMode/
    // setTarget/homing/moveToZero/selectSlave/estop
    void remoteCommand(const QString& name, const QJsonObject& args);

signals:
    void connectionChanged(bool connected, QString busName, int slaveCount, QString error);
    void telemetryUpdatedAll(const QList<Joint::Telemetry>& list);      // 批量遥测
    void slavesDetected(const QList<quint16>& slaves, quint16 activeSlave);
    // 各从站识别出的型号，下标 i 对应 slaveList()[i]（即从站 i+1）。
    // shortNames 用于标签页/下拉（如 "70mm"）；modelInfos 是含额定值的完整描述
    void slaveModelsDetected(const QStringList& shortNames, const QStringList& modelInfos);
    void faultDetected(QString message);
    void detectionMessage(QString message);   // 自动检测过程提示（显示在状态栏）
    void homingFinished(bool ok);             // 归航完成（ok=false 表示失败/超时）
    void limitExceeded(QString message);      // 超出行程限位（已自动停止）
    // 控制权变化。owner: "local"/"remote"；reason 给界面提示用
    void controlOwnerChanged(QString owner, QString reason);
    void remoteCommandRejected(QString name, QString reason);   // 远程命令被拒（无控制权等）

private slots:
    void onCycle();

private:
    bool tryOpen(const AppConfig& c);          // 尝试打开候选设备，成功则接管并启动周期
    void detectAndConnect();                   // Auto：EtherCAT → CANopen → 仿真

    // 命令来源校验。急停不走这里——安全命令不该被权限挡住。
    bool mayCommand(bool fromRemote) const;
    // 控制权切换一律先失能所有轴：否则接手方会继承一个"目标未知但仍在运动"的轴
    void disableAllAxes();
    void grantRemote(const QString& reason);
    void revokeRemote(const QString& reason);

    std::unique_ptr<JointDevice> device_;
    QTimer cycleTimer_;
    AppConfig cfg_;
    bool connected_ = false;
    quint16 activeSlave_ = 1;
    qint64 lastTelemetryMs_ = 0;
    bool limitWarned_ = false;   // 超限告警去抖：仅上升沿提示一次

    // ---- 控制权 ----
    ControlOwner owner_ = ControlOwner::Local;   // 默认本地（下位机在设备旁，本地优先）
    bool remoteAllowed_ = false;                 // 本地是否允许远程接管
    qint64 lastHeartbeatMs_ = 0;                 // 远程心跳时间戳
    quint64 heartbeatCount_ = 0;                 // 收到的心跳总数（诊断用）
};

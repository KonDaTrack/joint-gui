#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include <QJsonObject>
#include <memory>
#include "device/JointDevice.h"
#include "core/AppConfig.h"

class LoadController;   // 前向声明够了：只用作指针成员，实现在 .cpp 里包含

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
    // 「停止运动」单独立一条路：它和「下发目标」原来共用 targetRequested，
    // 而负载链挂在"下发目标"上——不拆开的话点停止会先写一次负载。
    void stopMotionRequested();
    // 网页的「预设」：只记住力矩值，等下次下发目标时才真正施加。不调 modbus_ao。
    void setLoadPresetRequested(double torqueNm);
    // 网页的「松开负载」：立刻写 0，不动关节。堵转时负载会一直保持（无超时），
    // 没有这个入口操作者就只能靠失能/急停撤载，而那会把关节一起失能。
    void releaseLoadRequested();

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
    // 波形记录统一由 worker 触发：本地和远程两条路都要覆盖，
    // 否则从网页下发目标时 Qt 端曲线不会开始记录
    void targetCommanded();   // 已下发目标 → 开始记录本次响应
    void motionStopped();     // 停止/失能 → 停止记录
    // 负载（磁粉制动器）状态如实回报。走 RS485 → Modbus → 0-10V，
    // 与关节的 EtherCAT 是**两条独立链路**。
    // state: preset(仅预设) / writing(写入中) / applied(已生效) / cleared(已清零) / failed(失败)
    // appliedNm 为 -1 表示"外设实际状态未知"——失败时不能假装是 0。
    void loadStateChanged(QString state, double presetNm, double appliedNm,
                          double volt, QString note);

private slots:
    void onCycle();
    void onLoadWriteFinished(double nm, bool ok, const QString& note);

private:
    bool tryOpen(const AppConfig& c);          // 尝试打开候选设备，成功则接管并启动周期
    void detectAndConnect();                   // Auto：EtherCAT → CANopen → 仿真

    // 命令来源校验。急停不走这里——安全命令不该被权限挡住。
    // 只在两个入口检查：本地的槽 + remoteCommand；下面的 do* 实现里**绝不**再查，
    // 否则远程路径会被第二次检查挡掉（踩过：使能可用但下发目标毫无反应）。
    bool mayCommand(bool fromRemote) const;
    void doSelectSlave(quint16 address);
    void doEnable();
    void doDisable();
    void doQuickStop();
    void doFaultReset();
    void doSetMode(Joint::OperateMode mode);
    void doSetTarget(const Joint::TargetCommand& cmd);
    void doHoming();
    void doMoveToZero();
    void doStopMotion();                       // 与 doSetTarget 分开，不带负载链
    void doSetLoadPreset(double torqueNm);     // 只记住预设值，不调 modbus_ao
    // 负载链：下发目标时"先写负载→确认成功→再发运动指令"
    void doSetTargetWithLoad(const Joint::TargetCommand& cmd, double loadNm);
    void armTargetAfterLoad(const Joint::TargetCommand& cmd, double nm);
    void beginLoadMotionWatch();
    // 所有"该撤载"路径的唯一出口（急停/失能/限位/换轴/断连/控制权切换）。
    // 它还会取消"写完负载再发目标"的待发状态——否则急停之后目标照样会发出去
    // （权限在点击那一刻有效，而写负载要几十~几百毫秒）。
    void clearLoad(const QString& reason);
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
    bool faultWarned_ = false;   // 驱动器故障上升沿去抖（用于撤载）

    // ---- 负载（磁粉制动器）----
    LoadController* load_ = nullptr;   // 在构造函数里 new（随 moveToThread 迁移）
    double loadPresetNm_ = 0.0;        // 预设值：只记住，下发目标时才施加
    bool loadTargetArmed_ = false;     // 有"等负载写完再发"的目标在等
    Joint::TargetCommand loadPendingTarget_;   // 覆盖语义，不排队
    bool loadMotionActive_ = false;    // 正在判定"本次运动是否结束"
    bool loadMoved_ = false;           // 先决条件：本次真的动过
    qint64 loadStillSinceMs_ = 0;      // 连续静止起点（用采样的 timestampMs）
    bool shuttingDown_ = false;        // 关设备中：拒绝一切新的异步写
    bool loadClearFailReported_ = false;   // "清负载失败"告警去抖

    // ---- 控制权 ----
    ControlOwner owner_ = ControlOwner::Local;   // 默认本地（下位机在设备旁，本地优先）
    bool remoteAllowed_ = false;                 // 本地是否允许远程接管
    qint64 lastHeartbeatMs_ = 0;                 // 远程心跳时间戳
    quint64 heartbeatCount_ = 0;                 // 收到的心跳总数（诊断用）
};

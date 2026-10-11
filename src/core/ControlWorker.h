#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include <QJsonObject>
#include <memory>
#include "device/JointDevice.h"
#include "core/AppConfig.h"

class LoadController;   // 前向声明够了：只用作指针成员，实现在 .cpp 里包含

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

    // ---- 上位机连接状态 ----
    // 现在**只有上位机一个控制端**（下位机默认无窗口，本地界面不存在），
    // 所以没有"控制权归属"要仲裁了 —— 谁连上就是谁。
    //
    // 但看门狗必须留着：上位机一掉（WS 断开，或网络半开导致心跳停），
    // 就失能所有轴 + 撤载。否则本地已经没有界面，没人发得出停止命令，
    // 只剩硬件急停可用。
    void setClientOnline(bool online);   // WS 客户端接入 / 断开
    void remoteHeartbeat();              // 上位机心跳（连接期间周期发送）
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
    void remoteCommandRejected(QString name, QString reason);   // 远程命令被拒（未知命令/设备未连等）
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
    // 上位机不再可用时的统一出口（WS 断开 / 心跳超时）
    void dropClient(const QString& reason);

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

    // ---- 上位机连接 ----
    bool clientOnline_ = false;                  // WS 客户端是否在线
    qint64 lastHeartbeatMs_ = 0;                 // 心跳时间戳
    quint64 heartbeatCount_ = 0;                 // 收到的心跳总数（诊断用）
};

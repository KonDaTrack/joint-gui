#include "core/ControlWorker.h"
#include "core/LoadController.h"
#include "device/DeviceFactory.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonObject>
#include <QNetworkInterface>

// 诊断：SDK 调用失败时会阻塞调用线程（实测约 2 秒/次），把同线程上的
// 心跳与遥测饿死。凡是超过该阈值的操作都打日志，用来定位到底是哪一次调用。
// 排查完可删。
static const qint64 kSlowOpWarnMs = 150;
static void warnIfSlow(const char* what, qint64 ms)
{
    if (ms > kSlowOpWarnMs)
        qDebug("[slow] %s 耗时 %lld ms（阻塞了心跳/遥测所在的线程）", what, ms);
}
// RAII 计时：在函数任意 return 路径都会结算
struct OpTimer {
    const char* what;
    qint64 t0;
    explicit OpTimer(const char* w) : what(w), t0(QDateTime::currentMSecsSinceEpoch()) {}
    ~OpTimer() { warnIfSlow(what, QDateTime::currentMSecsSinceEpoch() - t0); }
};

// 远程心跳超时（毫秒）。PC 端 200ms 一次。
//
// 为什么给到 3s 而不是 1s：心跳槽与超时判断都在工作线程上，而 SDK 的阻塞调用
// （eth_setControlWord 等状态迁移、eth_disable）会把该线程占住约 2 秒/次——
// 实测过驱动使能失败时连续报 "faile to Wait for state"，每次阻塞 ~2s。
// 那期间排队的心跳处理不了，1s 窗口会误判成"心跳超时"。
// 3s = 网络抖动的容忍 + 单次 SDK 阻塞的余量。
static const qint64 kRemoteHeartbeatTimeoutMs = 3000;

ControlWorker::ControlWorker(QObject* parent)
    : QObject(parent)
{
    // 关键：定时器必须作为子对象随 moveToThread 一起迁移到工作线程，
    // 否则它停留在创建线程（UI 线程），start/stop 变成跨线程调用且周期退化。
    // 用精确计时器：CoarseTimer 在 2ms 级周期下抖动明显，会导致 CSP 命令位置抖动 → 电机抖。
    cycleTimer_.setParent(this);
    cycleTimer_.setTimerType(Qt::PreciseTimer);
    connect(&cycleTimer_, &QTimer::timeout, this, &ControlWorker::onCycle);

    // 负载执行器同样作为子对象随 moveToThread 迁移。它内部的 QProcess 是**懒创建**的
    // （见 LoadController::ensureProc）——在这里 new 会先落在 UI 线程上，留下跨线程隐患。
    load_ = new LoadController(this);
    connect(load_, &LoadController::writeFinished, this, &ControlWorker::onLoadWriteFinished);
}

ControlWorker::~ControlWorker()
{
    disconnectDevice();
}

void ControlWorker::connectDevice(const AppConfig& cfg)
{
    disconnectDevice();
    cfg_ = cfg;

    if (cfg.busType == Joint::BusType::Auto) {
        detectAndConnect();
        return;
    }
    if (tryOpen(cfg)) return;
    emit connectionChanged(false, Joint::busTypeName(cfg.busType), 0,
                           QStringLiteral("连接失败"));
}

// 尝试打开候选设备；成功则接管 device_ 并启动周期循环，返回 true
bool ControlWorker::tryOpen(const AppConfig& c)
{
    device_ = createDevice(c.busType);
    if (!device_ || !device_->open(c)) {
        if (device_) device_->close();
        device_.reset();
        return false;
    }
    cfg_ = c;
    connected_ = true;
    // 重连后必须复位：disconnectDevice 会把它置真，不复位的话所有负载操作
    // （包括急停撤载）都会被静默拒绝，而且看不出原因。
    shuttingDown_ = false;
    loadClearFailReported_ = false;
    const QList<quint16> slaves = device_->slaveList();
    activeSlave_ = slaves.contains(c.slaveId) ? c.slaveId
                  : (slaves.isEmpty() ? 1 : slaves.first());
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    cycleTimer_.setInterval(cfg_.controlCycleMs());
    cycleTimer_.start();
    emit connectionChanged(true, Joint::busTypeName(cfg_.busType),
                           device_->slaveCount(), QString());
    emit slavesDetected(slaves, activeSlave_);
    QStringList shorts, models;
    for (quint16 s : slaves) {
        shorts << device_->modelShortName(s);
        models << device_->modelInfo(s);
    }
    emit slaveModelsDetected(shorts, models);
    return true;
}

// Auto 检测顺序：EtherCAT（遍历活动非回环网卡）→ CANopen → 仿真
void ControlWorker::detectAndConnect()
{
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifaces) {
        const QNetworkInterface::InterfaceFlags f = iface.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) ||
            !f.testFlag(QNetworkInterface::IsRunning) ||
            f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        emit detectionMessage(QStringLiteral("检测 EtherCAT 网卡 %1 ...").arg(iface.name()));
        AppConfig c = cfg_;
        c.busType = Joint::BusType::EtherCat;
        c.ethInterface = iface.name();
        if (tryOpen(c)) return;   // 失败时 device_ 已释放，继续下一个网卡
    }

    emit detectionMessage(QStringLiteral("未检测到 EtherCAT 从站，尝试 CANopen ..."));
    AppConfig c = cfg_;
    c.busType = Joint::BusType::CanOpen;
    if (tryOpen(c)) return;

    emit detectionMessage(QStringLiteral("未检测到 CANopen 设备，使用仿真模式"));
    AppConfig s = cfg_;
    s.busType = Joint::BusType::Simulation;
    if (!tryOpen(s)) {
        emit connectionChanged(false, QString(), 0, QStringLiteral("自动检测失败"));
    }
}

void ControlWorker::selectSlave(quint16 address)
{
    // 转调 doSelectSlave，只留一份逻辑——这里是重复实现，而 MainWindow 用
    // invokeMethod 按名字调用的正是本函数，两边容易改漏一处。
    doSelectSlave(address);
}

void ControlWorker::disconnectDevice()
{
    cycleTimer_.stop();
    // 断开时要作废待发的负载-目标链：否则"写负载中 → 断连 → 进程关掉"之后，
    // 那条链还挂着，重连后会莫名其妙发一次目标出去
    loadTargetArmed_ = false;
    loadMotionActive_ = false;

    // 只在第一次断开时同步清负载。为什么可以在这里阻塞：周期定时器已停、
    // 且本函数是由 MainWindow 用 BlockingQueuedConnection 在**设备线程**里调的，
    // 是唯一安全的时机。退出前必须确认制动器松开——进程一走 AO 模块会保持
    // 最后写入的电压，而模块自身没有看门狗。
    // （第二次调用来自 ~ControlWorker，那时已经在 UI 线程了，必须跳过。）
    if (!shuttingDown_) {
        shuttingDown_ = true;
        if (load_) {
            const bool ok = load_->shutdownWriteZero(900);
            if (!ok) {
                emit loadStateChanged(QStringLiteral("failed"), loadPresetNm_, -1.0, 0.0,
                                      QStringLiteral("退出前未能确认负载已松开，"
                                                     "请手动执行 modbus_ao -c 4 -v 0"));
            }
        }
    }

    if (device_) {
        device_->close();
        device_.reset();
    }
    connected_ = false;
}

void ControlWorker::onCycle()
{
    OpTimer _t("onCycle");
    if (!device_ || !connected_) return;

    QList<Joint::Telemetry> list;
    bool activeOk = false;
    for (quint16 s : device_->slaveList()) {
        Joint::Telemetry t;
        if (device_->readTelemetry(s, t)) {
            list.append(t);
            if (s == activeSlave_) { activeOk = true; }
        } else {
            Joint::Telemetry fail;
            fail.slave = s;
            fail.connected = false;
            list.append(fail);
        }
    }
    emit telemetryUpdatedAll(list);

    // 行程限位告警（去抖，仅上升沿提示一次）：设备层已停住该模式的动作
    bool limitNow = false;
    for (const Joint::Telemetry& t : list) {
        if (t.connected && t.limitExceeded) { limitNow = true; break; }
    }
    if (limitNow && !limitWarned_) {
        limitWarned_ = true;
        // 关节已经顶在限位上（设备层已停住动作），再咬着制动器就是持续堵转发热
        clearLoad(QStringLiteral("触发限位"));
        emit limitExceeded(QStringLiteral("超出 ±%1° 行程限位，已自动停止（保护力矩传感器线束）")
                           .arg(cfg_.travelLimitDeg));
    } else if (!limitNow) {
        limitWarned_ = false;   // 回到范围内，恢复可再次告警
    }

    // ---- 负载：随运动结束自动清零 ----
    // 必须放在下面那句 `if (activeOk) return;` **之前**——放后面的话遥测正常
    // （最常见的情况）时这段永远不会执行。
    //
    // 判据复刻 CurvePanel（那边是同一套"运动完成"判定），但静止窗口取 1000ms
    // 而不是它的 500ms：这里判"结束"的后果是**撤掉制动器负载**，阻力矩会瞬间消失。
    // 若在"关节还顶着制动器低速爬行"时误判，关节会突然加速窜出去。
    // 画图上判早了只是停止绘图（无害），在这里是物理动作。
    if (loadMotionActive_) {
        for (const Joint::Telemetry& t : list) {
            if (t.slave != activeSlave_ || !t.connected) continue;
            const double kStillDps = 0.5;   // 与 CurvePanel 一致，远高于实测 ~0.05 的速度噪声
            if (qAbs(t.velocityDps) > kStillDps) {
                loadMoved_ = true;
                loadStillSinceMs_ = 0;
            } else if (loadMoved_) {
                // 先决条件：必须"动过"才算结束。从头到尾没动（堵转/驱动器没使能）
                // 就保持负载——这是用户明确要求的，不设超时。
                if (loadStillSinceMs_ == 0) {
                    loadStillSinceMs_ = t.timestampMs;
                } else if (t.timestampMs - loadStillSinceMs_ > 1000) {
                    clearLoad(QStringLiteral("运动结束，负载已清零"));
                }
            }
            break;
        }
    }

    // 驱动器故障上升沿 → 撤载。这和"堵转不清"不矛盾：堵转时驱动器是健康的，
    // 而 fault 是异常终止——那种情况下关节永远不会再动，负载咬着就只能手动干预了。
    bool faultNow = false;
    for (const Joint::Telemetry& t : list) {
        if (t.slave == activeSlave_ && t.connected && t.errorCode) { faultNow = true; break; }
    }
    if (faultNow && !faultWarned_) {
        faultWarned_ = true;
        clearLoad(QStringLiteral("驱动器故障"));
    } else if (!faultNow) {
        faultWarned_ = false;
    }

    // 规则 3：远程心跳超时 → 自动收回控制权。
    // 不这样做的话，远程持有时网络一断，PC 发不出停止命令、本地界面又是灰的，
    // 两端都停不了电机，只剩硬件急停。
    if (owner_ == ControlOwner::Remote) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (lastHeartbeatMs_ > 0 && nowMs - lastHeartbeatMs_ > kRemoteHeartbeatTimeoutMs) {
            // 带上累计心跳数：0 表示心跳根本没到达 worker（接收链路问题），
            // >0 表示到达过但中断了（发送侧或节流问题）
            revokeRemote(QStringLiteral("远程心跳超时（累计收到 %1 次），已自动收回控制权")
                         .arg(heartbeatCount_));
            emit faultDetected(QStringLiteral("远程心跳超时，控制权已收回本地"));
        }
    }

    if (activeOk) {
        lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    // 看门狗：activeSlave 读失败超 500ms
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastTelemetryMs_ && (now - lastTelemetryMs_) > 500) {
        clearLoad(QStringLiteral("连接断开"));
        connected_ = false;
        cycleTimer_.stop();
        device_->close();
        device_.reset();
        emit faultDetected(QStringLiteral("遥测超时，连接已断开"));
        emit connectionChanged(false, QString(), 0, QStringLiteral("遥测超时"));
    }
}

// ============================ 控制权 ============================
// 规则（见 docs/host-embedded-architecture.md）：
//   1. 仲裁权在本地（下位机）；远程只能"请求"，不能夺取
//   2. 任何控制权切换 → 立即失能所有轴（否则接手方继承一个目标未知的运动轴）
//   3. 远程心跳超时 → 自动收回（否则断网时两端都停不了电机）
//   4. 急停不受控制权限制

bool ControlWorker::mayCommand(bool fromRemote) const
{
    return fromRemote ? (owner_ == ControlOwner::Remote)
                      : (owner_ == ControlOwner::Local);
}

void ControlWorker::disableAllAxes()
{
    // 放在 device_ 判空**之前**：设备已经释放时（比如断连后）控制权切换同样必须撤载。
    // 这一处覆盖 grantRemote / revokeRemote，而后者又被心跳超时、
    // setRemoteAllowed(false)、远程主动交还三条路调用——是覆盖面最大的一处。
    clearLoad(QStringLiteral("控制权切换"));
    if (!device_) return;
    for (quint16 s : device_->slaveList()) device_->disable(s);
}

void ControlWorker::grantRemote(const QString& reason)
{
    OpTimer _t("grantRemote");
    if (owner_ == ControlOwner::Remote) return;
    owner_ = ControlOwner::Remote;
    lastHeartbeatMs_ = QDateTime::currentMSecsSinceEpoch();
    disableAllAxes();   // 规则 2：切换即失能
    emit controlOwnerChanged(QStringLiteral("remote"), reason);
}

void ControlWorker::revokeRemote(const QString& reason)
{
    OpTimer _t("revokeRemote");
    if (owner_ != ControlOwner::Remote) return;
    owner_ = ControlOwner::Local;
    disableAllAxes();   // 规则 2：切换即失能
    emit controlOwnerChanged(QStringLiteral("local"), reason);
}

void ControlWorker::setRemoteAllowed(bool allowed)
{
    remoteAllowed_ = allowed;
    // 关掉开关时若远程正持有，立即收回（本地优先）
    if (!allowed) revokeRemote(QStringLiteral("本地已关闭远程控制"));
}

void ControlWorker::requestRemoteControl()
{
    if (!remoteAllowed_) {
        emit remoteCommandRejected(QStringLiteral("requestControl"),
                                   QStringLiteral("本地未允许远程控制"));
        return;
    }
    grantRemote(QStringLiteral("远程客户端已接管"));
}

void ControlWorker::releaseRemoteControl()
{
    revokeRemote(QStringLiteral("远程客户端已交还控制"));
}

void ControlWorker::remoteHeartbeat()
{
    ++heartbeatCount_;
    lastHeartbeatMs_ = QDateTime::currentMSecsSinceEpoch();
}

void ControlWorker::remoteCommand(const QString& name, const QJsonObject& args)
{
    // 「预设负载」是纯记忆：不碰设备、不调 modbus_ao，所以放在 device_ 判空之前——
    // 设备没连上时也该记住，否则重连后预设就丢了。
    if (name == QLatin1String("setLoad")) {
        if (!mayCommand(true)) {
            emit remoteCommandRejected(name, QStringLiteral("当前无控制权"));
            return;
        }
        doSetLoadPreset(args.value(QStringLiteral("torqueNm")).toDouble());
        return;
    }

    if (!device_) return;

    // 急停例外：安全命令任何时候都放行，不受控制权限制
    if (name == QLatin1String("estop")) {
        doQuickStop();
        doDisable();
        emit detectionMessage(QStringLiteral("远程急停"));
        return;
    }

    if (!mayCommand(true)) {
        emit remoteCommandRejected(name, QStringLiteral("当前无控制权"));
        return;
    }

    // 一律调用 do*（**不带权限检查**的实现）。绝不能转交本地的槽——
    // 那些槽里还有一次 mayCommand(false)，远程调用时必然为 false，
    // 命令会被静默丢弃（踩过：使能能用但下发目标毫无反应）。
    if (name == QLatin1String("enable")) {
        doEnable();
    } else if (name == QLatin1String("disable")) {
        doDisable();
    } else if (name == QLatin1String("quickStop")) {
        doQuickStop();
    } else if (name == QLatin1String("faultReset")) {
        doFaultReset();
    } else if (name == QLatin1String("selectSlave")) {
        doSelectSlave(static_cast<quint16>(args.value(QStringLiteral("slave")).toInt()));
    } else if (name == QLatin1String("setMode")) {
        doSetMode(static_cast<Joint::OperateMode>(args.value(QStringLiteral("mode")).toInt()));
    } else if (name == QLatin1String("setTarget")) {
        Joint::TargetCommand c;
        c.hasPosition = args.contains(QStringLiteral("positionDeg"));
        c.positionDeg = args.value(QStringLiteral("positionDeg")).toDouble();
        c.hasVelocity = args.contains(QStringLiteral("velocityDps"));
        c.velocityDps = args.value(QStringLiteral("velocityDps")).toDouble();
        c.hasTorque = args.contains(QStringLiteral("torqueNm"));
        c.torqueNm = args.value(QStringLiteral("torqueNm")).toDouble();
        c.profileVelocity = args.value(QStringLiteral("profileVelocity")).toDouble();
        c.profileAcceleration = args.value(QStringLiteral("profileAcceleration")).toDouble();
        c.profileDeceleration = args.value(QStringLiteral("profileDeceleration")).toDouble();
        c.torqueSlopeNmPerSec = args.value(QStringLiteral("torqueSlope")).toDouble();
        // 负载值与目标**一起**发（同一条消息，避免 setLoad 与 setTarget 的竞态）。
        // 0 / 缺省 = 本次不带负载；若当前正咬着负载，会被撤掉再运动。
        doSetTargetWithLoad(c, args.value(QStringLiteral("loadNm")).toDouble());
    } else if (name == QLatin1String("stopMotion")) {
        // 单独一条命令：「停止运动」不能走 setTarget，否则会先写一次负载。
        doStopMotion();
    } else if (name == QLatin1String("releaseLoad")) {
        clearLoad(QStringLiteral("手动松开负载"));
    } else if (name == QLatin1String("homing")) {
        doHoming();
    } else if (name == QLatin1String("moveToZero")) {
        doMoveToZero();
    } else {
        emit remoteCommandRejected(name, QStringLiteral("未知命令"));
    }
}

// ============================ 命令实现（do*，不带权限检查） ============================
// 权限检查只放在两个入口：本地的槽、以及 remoteCommand。
// do* 里绝不能再查一次——否则远程路径会被第二次检查挡掉（踩过）。

void ControlWorker::doSelectSlave(quint16 address)
{
    // 忽略不在从站列表里的地址，避免命令发给不存在的从站并触发整总线断连
    if (device_ && !device_->slaveList().contains(address)) return;
    // 换轴前撤载：「运动是否结束」只看当前控制轴，换了轴判据就失去对象，
    // 负载会一直咬着（没有超时会兜底）。
    if (address != activeSlave_)
        clearLoad(QStringLiteral("切换控制轴"));
    activeSlave_ = address;
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();   // 重置看门狗计时
}

void ControlWorker::doEnable()
{
    OpTimer _t("doEnable");
    if (!device_) return;
    if (!device_->enable(activeSlave_)) {
        // 如实反馈：驱动器可能停在「禁止合闸」不接受控制字（STO/硬件使能未给、
        // 或故障未复位）。以前静默失败，界面看起来"点了没反应"，很难排查。
        emit detectionMessage(QStringLiteral(
            "使能失败：驱动器未进入「运行使能」。请检查故障码、STO/硬件使能信号，"
            "以及是否被官方调试工具占用控制权"));
    }
}
void ControlWorker::doDisable()
{
    OpTimer _t("doDisable");
    clearLoad(QStringLiteral("失能"));
    if (device_) device_->disable(activeSlave_);
    emit motionStopped();
}
void ControlWorker::doQuickStop()
{
    OpTimer _t("doQuickStop");
    clearLoad(QStringLiteral("急停"));
    if (device_) device_->quickStop(activeSlave_);
    emit motionStopped();
}
void ControlWorker::doFaultReset()
{
    OpTimer _t("doFaultReset");
    if (device_) device_->faultReset(activeSlave_);
}
void ControlWorker::doSetMode(Joint::OperateMode mode)
{
    OpTimer _t("doSetMode");
    if (device_) device_->setOperateMode(activeSlave_, mode);
}
void ControlWorker::doSetTarget(const Joint::TargetCommand& cmd)
{
    OpTimer _t("doSetTarget");
    if (!device_) return;
    device_->setTarget(activeSlave_, cmd);
    emit targetCommanded();   // 波形记录由 worker 统一触发，本地/远程两条路都覆盖
}
void ControlWorker::doHoming()
{
    OpTimer _t("doHoming");
    if (!device_ || !connected_) return;
    clearLoad(QStringLiteral("归零"));   // 带着制动器归零是顶着阻力跑
    emit detectionMessage(QStringLiteral("归航中，请稍候..."));
    // 归航是阻塞操作：期间暂停看门狗（lastTelemetryMs_ 归零复位），结束后恢复，
    // 避免归航耗时长于 500ms 时被误判为遥测超时而断开
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    const bool ok = device_->homing(activeSlave_);
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    emit homingFinished(ok);
}
void ControlWorker::doMoveToZero()
{
    OpTimer _t("doMoveToZero");
    if (!device_ || !connected_) return;
    clearLoad(QStringLiteral("回0"));
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    const bool ok = device_->moveToZero(activeSlave_);
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    emit detectionMessage(ok ? QStringLiteral("已下发回0（切轮廓位置模式走到 0°）")
                             : QStringLiteral("回0 失败"));
}

// ============================ 负载（磁粉制动器） ============================
// 链路：网页预设 N·m → 点「下发目标」→ **先写负载、确认成功** → 再发关节运动指令
//       → 运动结束自动清零。
// 走 RS485 → Modbus → 0-10V，与关节的 EtherCAT 是**两条独立链路**。
//
// 「确认成功」是真的确认：modbus_ao 原本丢弃应答、恒返回 0，
// "模块根本没接"也报成功——那边已经修好了（arm/modbus_ao.c 的 check_write_ack）。
// 板上的二进制必须用修过的源码重新编译，否则这里等到的确认是假的。

void ControlWorker::doSetLoadPreset(double torqueNm)
{
    loadPresetNm_ = qMax(0.0, torqueNm);
    // 只说"已预设"，不说"已生效"——真正施加发生在下发目标时。
    const double applied = load_ ? load_->appliedNm() : -1.0;
    if (loadPresetNm_ > 0.0) {
        emit loadStateChanged(QStringLiteral("preset"), loadPresetNm_, applied,
                              LoadController::voltForNm(loadPresetNm_),
                              QStringLiteral("已预设 %1 N·m（%2 V），点「下发目标」时施加")
                                  .arg(loadPresetNm_, 0, 'f', 1)
                                  .arg(LoadController::voltForNm(loadPresetNm_), 0, 'f', 2));
    } else {
        emit loadStateChanged(QStringLiteral("cleared"), 0.0, applied, 0.0,
                              QStringLiteral("预设已清零"));
    }
}

void ControlWorker::doStopMotion()
{
    // 停止类命令不走负载链，但必须撤载：负载咬着时关节停不下来，而且堵转场景下
    // "运动结束"判据永远不会触发（没动过 → moved_ 恒假，且按需求不设超时）。
    clearLoad(QStringLiteral("收到停止指令"));
    Joint::TargetCommand c;
    c.hasVelocity = true;
    c.velocityDps = 0.0;
    doSetTarget(c);
}

void ControlWorker::doSetTargetWithLoad(const Joint::TargetCommand& cmd, double loadNm)
{
    if (!device_ || shuttingDown_) return;

    // 不需要写负载就整条链跳过。这条很关键：预设为 0 且当前无负载时**压根不 fork**
    // modbus_ao —— 所以开发机/仿真模式（根本没有这个工具）完全不受影响，
    // 也不会因为工具缺失就把关节锁死。
    const bool needWrite = (loadNm > 0.0) || (load_ && load_->appliedNm() > 0.0);
    if (!needWrite) {
        doSetTarget(cmd);
        beginLoadMotionWatch();
        return;
    }
    armTargetAfterLoad(cmd, loadNm);
}

void ControlWorker::armTargetAfterLoad(const Joint::TargetCommand& cmd, double nm)
{
    if (LoadController::outOfRange(nm)) {
        // 硬拒绝而不是夹到量程：静默施加一个与请求不同的负载，比直接失败危险得多
        // （操作者会照着一个假数据做判断）。同时拦住运动——负载没按请求加上就跑，
        // 产生的是一组"看着正常其实不对"的实验数据。
        emit loadStateChanged(QStringLiteral("failed"), loadPresetNm_, -1.0,
                              LoadController::voltForNm(nm),
                              QStringLiteral("%1 N·m 超出模块量程（最大 %2 N·m / 10V），未下发")
                                  .arg(nm, 0, 'f', 1)
                                  .arg(LoadController::kRatedNm, 0, 'f', 0));
        return;
    }

    loadPendingTarget_ = cmd;   // 覆盖语义：连点多次只发最后一个目标
    loadTargetArmed_ = true;
    emit loadStateChanged(QStringLiteral("writing"), loadPresetNm_, -1.0,
                          LoadController::voltForNm(nm),
                          QStringLiteral("正在写入负载 %1 N·m（%2 V）…")
                              .arg(nm, 0, 'f', 1)
                              .arg(LoadController::voltForNm(nm), 0, 'f', 2));
    load_->requestWrite(nm);
}

void ControlWorker::onLoadWriteFinished(double nm, bool ok, const QString& note)
{
    if (ok && nm > 0.0)
        loadClearFailReported_ = false;

    if (!loadTargetArmed_) {
        // 不是"下发目标"那条链（撤载、或已被安全路径取消）
        if (!ok && nm <= 0.0) {
            // 清负载失败 = 制动器可能还咬着，而操作者以为已经松开。值得一次模态告警，
            // 但要 latch：失能/急停每次都会清负载，工具缺失时会变成弹窗风暴。
            emit loadStateChanged(QStringLiteral("failed"), loadPresetNm_, -1.0, 0.0, note);
            if (!loadClearFailReported_) {
                loadClearFailReported_ = true;
                emit faultDetected(note);
            }
        } else {
            emit loadStateChanged(ok ? (nm > 0.0 ? QStringLiteral("applied")
                                                 : QStringLiteral("cleared"))
                                     : QStringLiteral("failed"),
                                  loadPresetNm_, ok ? nm : -1.0,
                                  LoadController::voltForNm(nm), note);
        }
        return;
    }

    emit loadStateChanged(ok ? QStringLiteral("applied") : QStringLiteral("failed"),
                          loadPresetNm_, ok ? nm : -1.0,
                          LoadController::voltForNm(nm), note);

    if (!ok) {
        // 拦住不发运动（用户明确选择）。用状态栏而不是 faultDetected：
        // 那个会弹模态框，而这条路径在工具缺失时会反复触发。
        loadTargetArmed_ = false;
        emit detectionMessage(QStringLiteral("负载未生效，已拦住运动指令：%1").arg(note));
        return;
    }

    loadTargetArmed_ = false;
    doSetTarget(loadPendingTarget_);
    beginLoadMotionWatch();
}

void ControlWorker::beginLoadMotionWatch()
{
    loadMoved_ = false;
    loadStillSinceMs_ = 0;
    // 只有真的施加了负载才需要判"运动结束"（没负载时判了也没意义）
    loadMotionActive_ = load_ && load_->appliedNm() > 0.0;
}

void ControlWorker::clearLoad(const QString& reason)
{
    // 先取消待发目标：否则急停之后那条"写完负载再发目标"的链照样会把目标发出去
    // ——权限是在点击那一刻查的，而写负载要几十~几百毫秒，这期间控制权可能已易主。
    loadTargetArmed_ = false;
    loadPendingTarget_ = Joint::TargetCommand();
    loadMotionActive_ = false;
    loadMoved_ = false;
    loadStillSinceMs_ = 0;

    if (!load_ || shuttingDown_) return;
    // 没有负载、也没有在飞的写 → 不必 fork（开发机上常走这条）
    if (load_->appliedNm() <= 0.0 && !load_->busy() && !load_->hasPending()) return;

    emit loadStateChanged(QStringLiteral("writing"), loadPresetNm_, -1.0, 0.0,
                          QStringLiteral("%1（正在释放负载…）").arg(reason));
    load_->requestWrite(0.0);
}

// ============================ 本地命令槽（带权限检查） ============================

void ControlWorker::enableRequested()
{
    if (!mayCommand(false)) return;
    doEnable();
}
void ControlWorker::disableRequested()
{
    doDisable();     // 失能任何时候都允许
}
void ControlWorker::quickStopRequested()
{
    doQuickStop();   // 停止类任何时候都允许
}
void ControlWorker::faultResetRequested()
{
    if (!mayCommand(false)) return;
    doFaultReset();
}
void ControlWorker::setOperateModeRequested(Joint::OperateMode mode)
{
    if (!mayCommand(false)) return;
    doSetMode(mode);
}
void ControlWorker::setTargetRequested(const Joint::TargetCommand& cmd)
{
    if (!mayCommand(false)) return;
    // 本地下发一律**不带负载**（loadNm = 0）：站在设备旁边的操作者，不该被
    // 网页上设的负载突然咬住。但若当前正有负载在咬着，"没有新负载"会先把它撤掉
    // 再运动——见 doSetTargetWithLoad 里的 needWrite 判断。
    doSetTargetWithLoad(cmd, 0.0);
}
void ControlWorker::stopMotionRequested()
{
    if (!mayCommand(false)) return;
    doStopMotion();
}
void ControlWorker::setLoadPresetRequested(double torqueNm)
{
    if (!mayCommand(false)) return;
    doSetLoadPreset(torqueNm);
}
void ControlWorker::releaseLoadRequested()
{
    if (!mayCommand(false)) return;
    clearLoad(QStringLiteral("手动松开负载"));
}
void ControlWorker::homingRequested()
{
    if (!mayCommand(false)) return;
    doHoming();
}
void ControlWorker::moveToZeroRequested()
{
    if (!mayCommand(false)) return;
    doMoveToZero();
}

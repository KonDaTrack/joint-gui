#include "core/ControlWorker.h"
#include "device/DeviceFactory.h"
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkInterface>

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
    // 忽略不在从站列表里的地址，避免命令发给不存在的从站并触发整总线断连
    if (device_ && !device_->slaveList().contains(address)) return;
    activeSlave_ = address;
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();   // 重置看门狗计时
}

void ControlWorker::disconnectDevice()
{
    cycleTimer_.stop();
    if (device_) {
        device_->close();
        device_.reset();
    }
    connected_ = false;
}

void ControlWorker::onCycle()
{
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
        emit limitExceeded(QStringLiteral("超出 ±%1° 行程限位，已自动停止（保护力矩传感器线束）")
                           .arg(cfg_.travelLimitDeg));
    } else if (!limitNow) {
        limitWarned_ = false;   // 回到范围内，恢复可再次告警
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
    if (!device_) return;
    for (quint16 s : device_->slaveList()) device_->disable(s);
}

void ControlWorker::grantRemote(const QString& reason)
{
    if (owner_ == ControlOwner::Remote) return;
    owner_ = ControlOwner::Remote;
    lastHeartbeatMs_ = QDateTime::currentMSecsSinceEpoch();
    disableAllAxes();   // 规则 2：切换即失能
    emit controlOwnerChanged(QStringLiteral("remote"), reason);
}

void ControlWorker::revokeRemote(const QString& reason)
{
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
        doSetTarget(c);
    } else if (name == QLatin1String("homing")) {
        doHoming();
    } else if (name == QLatin1String("moveToZero")) {
        doMoveToZero();
    } else if (name == QLatin1String("setLoad")) {
        // 负载走 RS485（独立于 EtherCAT），硬件后端尚未接线。
        // 只回报"收到"，由界面如实提示——不能让人以为负载已经生效。
        emit loadCommandReceived(args.value(QStringLiteral("torqueNm")).toDouble(),
                                 args.value(QStringLiteral("volt")).toDouble());
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
    activeSlave_ = address;
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();   // 重置看门狗计时
}

void ControlWorker::doEnable()
{
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
    if (device_) device_->disable(activeSlave_);
    emit motionStopped();
}
void ControlWorker::doQuickStop()
{
    if (device_) device_->quickStop(activeSlave_);
    emit motionStopped();
}
void ControlWorker::doFaultReset()
{
    if (device_) device_->faultReset(activeSlave_);
}
void ControlWorker::doSetMode(Joint::OperateMode mode)
{
    if (device_) device_->setOperateMode(activeSlave_, mode);
}
void ControlWorker::doSetTarget(const Joint::TargetCommand& cmd)
{
    if (!device_) return;
    device_->setTarget(activeSlave_, cmd);
    emit targetCommanded();   // 波形记录由 worker 统一触发，本地/远程两条路都覆盖
}
void ControlWorker::doHoming()
{
    if (!device_ || !connected_) return;
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
    if (!device_ || !connected_) return;
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    const bool ok = device_->moveToZero(activeSlave_);
    lastTelemetryMs_ = QDateTime::currentMSecsSinceEpoch();
    emit detectionMessage(ok ? QStringLiteral("已下发回0（切轮廓位置模式走到 0°）")
                             : QStringLiteral("回0 失败"));
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
    doSetTarget(cmd);
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

#include "core/LoadController.h"

#include <QProcess>
#include <QTimer>
#include <QFileInfo>

namespace {
// AO 通道：CH4（寄存器 0x0053）。用户确认目前只用这一路。
constexpr int kChannel = 4;

// 子进程自己的应答等待超时（对应 modbus_ao 的 -t）。模块正常时几毫秒就回，
// 这只是"没插模块"时早点失败，别让操作者干等。
constexpr int kProcTimeoutMs = 500;

// 看门狗：兜"子进程卡死"（比如串口 ioctl 挂住）。modbus_ao 自己会退出，
// 这里只是防止一次挂死把状态机永久卡在 Writing——那会导致此后每次下发目标都毫无反应。
constexpr int kWatchdogMs = 2500;
}   // namespace

LoadController::LoadController(QObject* parent)
    : QObject(parent)
{
    // 必须 setParent(this)：QTimer 只有是**子对象**时才会随 moveToThread 迁移。
    // 作为值成员或裸 new 而不设 parent，定时器会留在 UI 线程，
    // 在工作线程 start() 时会报 "Timers can only be used with threads started with QThread"。
    // （ControlWorker 里 cycleTimer_ 有同样的一行，注释也在那儿。）
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, &LoadController::onWatchdog);
}

QString LoadController::toolPath() const
{
    // 为什么是绝对路径而不是 "~/rs485/modbus_ao"：
    //  1) QProcess 不经过 shell，"~" 是字面量，不会被展开；
    //  2) 程序是用 sudo 起的（EtherCAT 要 raw socket），$HOME 是 /root，
    //     用 QDir::homePath() 会指到 /root/rs485 而不是 /home/cat/rs485。
    // 留一个环境变量覆盖，换板子/开发机调试时不用重编译。
    const QByteArray env = qgetenv("JOINT_LOAD_TOOL");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QStringLiteral("/home/cat/rs485/modbus_ao");
}

void LoadController::ensureProc()
{
    if (proc_)
        return;
    // 懒创建：第一次真正要写的时候才 new。LoadController 是在 ControlWorker 的
    // 构造函数里创建的，而那个构造函数跑在 UI 线程——那时 new QProcess 会先落在
    // UI 线程，之后靠 moveToThread 迁移，留下跨线程 notifier 的隐患。
    // 在这里创建则从一开始就属于调用线程（设备线程）。
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &LoadController::onProcFinished);
    connect(proc_, &QProcess::errorOccurred, this, &LoadController::onProcError);
}

void LoadController::requestWrite(double nm)
{
    wantNm_ = qMax(0.0, nm);
    wantValid_ = true;
    if (phase_ == Phase::Idle)
        startNext();
    // 正在写就只是更新 wantNm_：开始下一个时会取最新值（最后写入者胜）
}

void LoadController::startNext()
{
    if (!wantValid_) {
        phase_ = Phase::Idle;
        return;
    }

    const double nm = wantNm_;
    wantValid_ = false;
    procNm_ = nm;
    procDone_ = false;
    timedOut_ = false;
    phase_ = Phase::Writing;

    ensureProc();

    const int mv = qBound(0, mvForNm(nm), kMaxMv);   // 越量程已在调用方拒绝，这里只兜底
    watchdog_->start(kWatchdogMs);

    // 状态必须全部就位**之后**才 start()：FailedToStart 可能是 start() 内部
    // 同步发出的，若那时 procDone_/watchdog_ 还没准备好，这次完成会被漏掉，
    // 状态机就永久卡在 Writing 了。
    proc_->start(toolPath(),
                 { QStringLiteral("-c"), QString::number(kChannel),
                   QStringLiteral("-v"), QString::number(mv),
                   QStringLiteral("-t"), QString::number(kProcTimeoutMs) });
}

void LoadController::onProcFinished(int exitCode, QProcess::ExitStatus status)
{
    if (procDone_)
        return;   // Crashed 时 errorOccurred 已经处理过
    procDone_ = true;
    watchdog_->stop();

    // 失败原因在 stderr（modbus_ao 把"无应答""异常码"都打在这里）
    const QString err = QString::fromLocal8Bit(proc_->readAllStandardError()).trimmed();

    const bool ok = (status == QProcess::NormalExit && exitCode == 0);
    QString note;
    if (ok) {
        note = QStringLiteral("已写入 CH%1 = %2 mV（%3 N·m）")
                   .arg(kChannel).arg(mvForNm(procNm_)).arg(procNm_, 0, 'f', 1);
    } else if (timedOut_) {
        note = QStringLiteral("modbus_ao 超时未返回，已终止");
    } else {
        note = QStringLiteral("modbus_ao 失败（退出码 %1）：%2")
                   .arg(exitCode)
                   .arg(err.isEmpty() ? QStringLiteral("无输出") : err);
    }
    finishWrite(ok, note);
}

void LoadController::onProcError(QProcess::ProcessError err)
{
    // 只处理 FailedToStart；Crashed/Timeout 交给 finished 统一走一遍
    if (err != QProcess::FailedToStart || procDone_)
        return;
    procDone_ = true;
    watchdog_->stop();
    finishWrite(false,
                QStringLiteral("无法启动 %1：%2（确认板上已编译好、且有执行权限）")
                    .arg(toolPath(), proc_->errorString()));
}

void LoadController::onWatchdog()
{
    if (phase_ != Phase::Writing || procDone_)
        return;
    timedOut_ = true;
    proc_->kill();   // 触发 finished（Crashed），在 onProcFinished 里统一收尾
}

void LoadController::finishWrite(bool ok, const QString& note)
{
    const double nm = procNm_;
    appliedNm_ = ok ? nm : -1.0;   // 失败 = 外设实际状态未知，不能假装是 0
    phase_ = Phase::Idle;
    emit writeFinished(nm, ok, note);

    // 排队推进而不是直接调用：如果刚才是 start() 里的同步回调，
    // 直接递归会在 start() 返回前又发一次进程，栈和状态都容易乱。
    QMetaObject::invokeMethod(this, "startNext", Qt::QueuedConnection);
}

bool LoadController::shutdownWriteZero(int waitMs)
{
    wantValid_ = false;   // 丢掉一切排队的写
    watchdog_->stop();

    // 在飞的（可能是非零负载）先掐掉
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        proc_->waitForFinished(300);   // 已 kill，几乎立刻返回
    }
    ensureProc();

    proc_->start(toolPath(),
                 { QStringLiteral("-c"), QString::number(kChannel),
                   QStringLiteral("-v"), QStringLiteral("0"),
                   QStringLiteral("-t"), QStringLiteral("300") });
    const bool ok = proc_->waitForFinished(waitMs) && proc_->exitCode() == 0;
    appliedNm_ = ok ? 0.0 : -1.0;

    // 同线程、且此刻没有在飞的进程 → 直接删是安全的。
    // 这一步是必须的：MainWindow::~MainWindow 会在 UI 线程里 delete worker_，
    // 若那时 QProcess 还在，就变成跨线程析构（Qt 会告警并 kill）。
    delete proc_;
    proc_ = nullptr;
    return ok;
}

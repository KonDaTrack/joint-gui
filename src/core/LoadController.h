#pragma once
#include <QObject>
#include <QString>
// 必须是完整类型，不能前置声明：下面的槽签名用了 QProcess::ExitStatus /
// QProcess::ProcessError 这两个嵌套枚举，moc 生成的代码要能解析它们。
#include <QProcess>

class QTimer;

// 磁粉制动器负载执行器：N·m → mV，调外部程序写 RS485/Modbus 的 0-10V 模块。
//
// 为什么不在 Qt 里直接开串口：modbus_ao 的报文、CRC、寄存器映射都已与模块手册核对过，
// 在 Qt 里重写等于再引入一份可能与模块不一致的实现。代价是每次写要 fork/exec 一次
// （几毫秒），但串口 I/O 全在子进程里，工作线程只付 fork 的钱。
//
// 三条铁律：
//   1) 同一时刻最多一个子进程。这是串口不是 socket，两个进程抢 /dev/ttyS3 会让帧交错，
//      模块收到的电压不可预测。
//   2) 合并语义：**最后写入者胜**，不排队。排队会让「急停写 0」排在「写 30 N·m」后面，
//      语义就反了。
//   3) 除了 shutdownWriteZero()，**绝不 waitForFinished()**——那会阻塞设备线程，
//      而设备线程被阻塞会导致心跳误判（本项目踩过）。
//
// 所有方法都必须在同一个线程里调用（即 ControlWorker 所在线程）。
class LoadController : public QObject
{
    Q_OBJECT
public:
    explicit LoadController(QObject* parent = nullptr);

    // ---- 标定 ----
    // 制动器 50 N·m ↔ 10V（用户确认，且 0-12V 那档用不满）。
    // 模块数值单位是 mV，0~10000 对应 0~10V，所以 mV = N·m × 200。
    static constexpr double kRatedNm = 50.0;
    static constexpr int    kMaxMv   = 10000;

    static int    mvForNm(double nm)    { return qRound(nm * 200.0); }
    static double voltForNm(double nm)  { return nm / 5.0; }
    // 越量程由调用方**拒绝**而不是夹取：静默施加一个与请求不同的负载，
    // 比直接失败危险得多（操作者会照着一个假数据做判断）。
    static bool   outOfRange(double nm) { return mvForNm(nm) > kMaxMv || nm < 0.0; }

    double appliedNm() const { return appliedNm_; }   // 已确认写入的值，-1 = 未知/从未写成功
    bool   busy() const      { return phase_ == Phase::Writing; }
    bool   hasPending() const { return wantValid_; }

    // 请求写一个值（nm <= 0 表示释放）。非阻塞；已有写在飞时按合并语义处理。
    void requestWrite(double nm);

    // 仅退出/关设备时用：同步写 0 并等待，然后同步销毁 QProcess。
    // **这是唯一允许阻塞的地方**——必须在设备线程、且周期定时器已停之后调用。
    // 进程一走 AO 模块会保持最后写入的电压（模块自身没有看门狗），所以退出前
    // 必须确认制动器已松开。
    bool shutdownWriteZero(int waitMs);

signals:
    // 一次写的最终结果。ok=false 时 note 里是可直接展示给操作者的原因。
    void writeFinished(double nm, bool ok, const QString& note);

private slots:
    void onProcFinished(int exitCode, QProcess::ExitStatus status);
    void onProcError(QProcess::ProcessError error);
    void onWatchdog();
    // 必须是**槽**：finishWrite() 用 QMetaObject::invokeMethod(this, "startNext", ...)
    // 排队推进（为了不在 start() 的同步回调栈里递归）。按名字调用只能找到槽，
    // 普通私有方法会静默失败 —— 后果是写负载期间来的新请求（含急停写 0）被永久丢掉，
    // 制动器一直咬着旧值。单测 coalescingKeepsLastValue 就是抓这个的。

    void startNext();

private:
    enum class Phase { Idle, Writing };

    void ensureProc();
    void finishWrite(bool ok, const QString& note);
    QString toolPath() const;

    QProcess* proc_ = nullptr;   // 懒创建：见 ensureProc() 的注释
    QTimer*   watchdog_ = nullptr;
    Phase     phase_ = Phase::Idle;
    double    wantNm_ = 0.0;      // 待写值（合并后的最新请求）
    bool      wantValid_ = false;
    double    procNm_ = 0.0;      // 在飞进程正在写的值
    bool      procDone_ = false;  // 本次完成的哨兵：finished/errorOccurred 只处理一次
    bool      timedOut_ = false;  // 本次是被看门狗掐掉的
    double    appliedNm_ = -1.0;  // 已确认写入（-1 = 未知/从未写成功）
};

#include "ui/MainWindow.h"
#include "ui/MonitorPanel.h"
#include "ui/ControlPanel.h"
#include "ui/CurvePanel.h"
#include "ui/ConnectionDialog.h"
#include "core/ControlServer.h"
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

// 上位机接入端口。P1 固定，后续可做成配置项。
static const quint16 kRemotePort = 9002;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("关节模组监控台"));
    resize(1680, 940);   // 适配 1080p 屏摄，保持可缩放

    worker_ = new ControlWorker;
    worker_->moveToThread(&thread_);
    thread_.start();

    // 上位机接入服务：跑在独立线程。端口占用/客户端异常都只影响远程功能，
    // 本地控制不受任何影响（这是"下位机必须可靠"的前提）。
    server_ = new ControlServer;
    server_->moveToThread(&serverThread_);
    serverThread_.start();
    QMetaObject::invokeMethod(server_, "startServer", Qt::QueuedConnection,
                              Q_ARG(quint16, kRemotePort));

    monitor_ = new MonitorPanel(this);
    control_ = new ControlPanel(this);
    curve_ = new CurvePanel(this);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(8);
    splitter->addWidget(monitor_);
    // ControlPanel 较高（约 480px），放入滚动区防止在 ~540px 分栏高度下被裁剪。
    QScrollArea* ctrlScroll = new QScrollArea(this);
    ctrlScroll->setWidget(control_);
    ctrlScroll->setWidgetResizable(true);
    ctrlScroll->setFrameShape(QFrame::NoFrame);
    ctrlScroll->viewport()->setAutoFillBackground(false);
    splitter->addWidget(ctrlScroll);
    splitter->setStretchFactor(0, 3);   // 监控 : 控制 ≈ 3:2
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({660, 420});

    QWidget* center = new QWidget(this);
    center->setObjectName(QStringLiteral("centralRoot"));
    center->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* lay = new QVBoxLayout(center);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);
    lay->addWidget(splitter, 3);   // 上区(监控+控制) : 曲线 ≈ 3:2
    lay->addWidget(curve_, 2);
    setCentralWidget(center);

    connect(worker_, &ControlWorker::telemetryUpdatedAll, monitor_, &MonitorPanel::onTelemetry);
    connect(worker_, &ControlWorker::telemetryUpdatedAll, curve_, &CurvePanel::onTelemetry);
    connect(worker_, &ControlWorker::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(worker_, &ControlWorker::faultDetected, this, &MainWindow::onFaultDetected);
    connect(worker_, &ControlWorker::detectionMessage, this,
            [this](const QString& m) { statusBar()->showMessage(m); });
    connect(worker_, &ControlWorker::slavesDetected, this,
            [this](const QList<quint16>& slaves, quint16 active) {
                monitor_->setSlaves(slaves);
                monitor_->setActiveSlave(active);
                control_->setSlaves(slaves);
                control_->setActiveSlave(active);
                curve_->setActiveSlave(active);
            });
    connect(worker_, &ControlWorker::slaveModelsDetected, this,
            [this](const QStringList& shorts, const QStringList& infos) {
                monitor_->setSlaveModels(shorts, infos);
                control_->setSlaveModels(shorts);   // 下拉项也带型号，控制目标一眼可辨
            });

    // 从站选择三处统一（左侧标签页 / 右侧下拉 / 下方曲线 + worker 的控制目标）：
    // 任一控件切换都同步其余，避免"看着从站2、命令发给从站1"的错位
    auto switchSlave = [this](quint16 a) {
        monitor_->setActiveSlave(a);   // 均为程序化同步（内部屏蔽信号），不会回环
        control_->setActiveSlave(a);
        curve_->setActiveSlave(a);
        QMetaObject::invokeMethod(worker_, "selectSlave", Qt::QueuedConnection, Q_ARG(quint16, a));
    };
    // 切换入口只有左侧标签页（右侧控制面板只显示，避免两个入口不同步）
    connect(monitor_, &MonitorPanel::activeSlaveChanged, this, switchSlave);
    // 控制目标切换时状态栏提示，强化"命令会发给哪个轴"的感知
    connect(control_, &ControlPanel::controlTargetChanged, this, [this](const QString& label) {
        statusBar()->showMessage(QStringLiteral("控制目标已切换到 %1").arg(label), 5000);
    });

    // ---- 上位机接入 / 控制权 ----
    // 设备 → 服务（下行）
    connect(worker_, &ControlWorker::telemetryUpdatedAll, server_, &ControlServer::onTelemetry);
    connect(worker_, &ControlWorker::connectionChanged, server_, &ControlServer::onConnectionChanged);
    connect(worker_, &ControlWorker::slavesDetected, server_, &ControlServer::onSlavesDetected);
    connect(worker_, &ControlWorker::slaveModelsDetected, server_, &ControlServer::onSlaveModels);
    connect(worker_, &ControlWorker::faultDetected, server_, &ControlServer::onFault);
    connect(worker_, &ControlWorker::loadCommandReceived, server_, &ControlServer::onLoadCommand);
    // 服务 → 设备（上行）：控制权与命令
    connect(server_, &ControlServer::requestControlReceived, worker_, &ControlWorker::requestRemoteControl);
    connect(server_, &ControlServer::releaseControlReceived, worker_, &ControlWorker::releaseRemoteControl);
    connect(server_, &ControlServer::heartbeatReceived, worker_, &ControlWorker::remoteHeartbeat);
    connect(server_, &ControlServer::remoteCommandReceived, worker_, &ControlWorker::remoteCommand);
    // 控制权状态：本地开关 → 设备；设备结果 → 界面 + 服务
    connect(control_, &ControlPanel::remoteAllowedChanged, worker_, &ControlWorker::setRemoteAllowed);
    connect(worker_, &ControlWorker::controlOwnerChanged, this,
            [this](const QString& owner, const QString& reason) {
                control_->setControlOwner(owner);
                statusBar()->showMessage(
                    owner == QLatin1String("remote")
                        ? QStringLiteral("控制权已交给上位机：%1").arg(reason)
                        : QStringLiteral("控制权在本机：%1").arg(reason), 6000);
            });
    connect(worker_, &ControlWorker::controlOwnerChanged, server_, &ControlServer::onControlOwnerChanged);
    connect(worker_, &ControlWorker::remoteCommandRejected, this,
            [this](const QString& name, const QString& reason) {
                statusBar()->showMessage(QStringLiteral("上位机命令 %1 被拒：%2").arg(name, reason), 5000);
            });
    connect(server_, &ControlServer::serverLog, this,
            [this](const QString& m) { statusBar()->showMessage(m, 5000); });

    connect(control_, &ControlPanel::enableRequested, worker_, &ControlWorker::enableRequested);
    connect(control_, &ControlPanel::disableRequested, worker_, &ControlWorker::disableRequested);
    connect(control_, &ControlPanel::quickStopRequested, worker_, &ControlWorker::quickStopRequested);
    connect(control_, &ControlPanel::faultResetRequested, worker_, &ControlWorker::faultResetRequested);
    connect(control_, &ControlPanel::operateModeChanged, worker_, &ControlWorker::setOperateModeRequested);
    connect(control_, &ControlPanel::targetRequested, worker_, &ControlWorker::setTargetRequested);
    // 波形只在「下发目标」后记录，避免平时一直被噪声刷新。
    // 触发源放在 worker（而非 ControlPanel）：本地和远程两条命令路径都能覆盖到，
    // 否则从网页下发目标时 Qt 端曲线不会开始记录。
    connect(worker_, &ControlWorker::targetCommanded, curve_, &CurvePanel::beginCapture);
    connect(worker_, &ControlWorker::motionStopped, curve_, &CurvePanel::stopCapture);
    connect(control_, &ControlPanel::homingRequested, worker_, &ControlWorker::homingRequested);
    connect(control_, &ControlPanel::moveToZeroRequested, worker_, &ControlWorker::moveToZeroRequested);
    connect(worker_, &ControlWorker::homingFinished, this, [this](bool ok) {
        statusBar()->showMessage(ok ? QStringLiteral("归零完成") : QStringLiteral("归零失败"),
                                 5000);
    });
    connect(worker_, &ControlWorker::limitExceeded, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 8000);
    });

    statusBar()->showMessage(QStringLiteral("未连接"));

    QTimer::singleShot(0, this, &MainWindow::promptConnection);
}

MainWindow::~MainWindow()
{
    if (server_) {
        // 先关网络：在服务线程内关监听与连接，再退出线程
        QMetaObject::invokeMethod(server_, "stopServer", Qt::BlockingQueuedConnection);
        serverThread_.quit();
        if (!serverThread_.wait(1000)) {
            qWarning("Server thread did not stop within 1s; terminating");
            serverThread_.terminate();
            serverThread_.wait();
        }
        delete server_;
    }
    if (worker_) {
        // 在 Worker 线程内安全关闭设备，再退出线程
        QMetaObject::invokeMethod(worker_, "disconnectDevice", Qt::BlockingQueuedConnection);
        thread_.quit();
        if (!thread_.wait(2000)) {
            // 真实设备 close() 可能阻塞（Task 15/16 接入后），超时则强制终止以免删除活动线程
            qWarning("Worker thread did not stop within 2s; terminating");
            thread_.terminate();
            thread_.wait();
        }
        delete worker_;
    }
}

void MainWindow::promptConnection()
{
    ConnectionDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        const AppConfig cfg = dlg.config();
        QMetaObject::invokeMethod(worker_, "connectDevice",
                                  Qt::QueuedConnection, Q_ARG(AppConfig, cfg));
    }
}

void MainWindow::onConnectionChanged(bool connected, QString busName,
                                     int slaveCount, QString error)
{
    if (connected) {
        statusBar()->showMessage(QStringLiteral("%1 已连接，从站数 %2")
                                 .arg(busName).arg(slaveCount));
        // CANopen 仅支持力矩位置混合，限制模式下拉避免下发零刚度 MIT 帧
        control_->setBusType(busName == Joint::busTypeName(Joint::BusType::CanOpen)
                             ? Joint::BusType::CanOpen : Joint::BusType::EtherCat);
    } else if (!error.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("连接失败：%1").arg(error));
    } else {
        statusBar()->showMessage(QStringLiteral("已断开"));
    }
}

void MainWindow::onFaultDetected(QString message)
{
    QMessageBox::warning(this, tr("连接异常"), message);
}

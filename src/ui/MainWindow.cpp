#include "ui/MainWindow.h"
#include "ui/MonitorPanel.h"
#include "ui/ControlPanel.h"
#include "ui/CurvePanel.h"
#include "ui/ConnectionDialog.h"
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("关节模组监控台"));
    resize(1100, 700);

    worker_ = new ControlWorker;
    worker_->moveToThread(&thread_);
    thread_.start();

    monitor_ = new MonitorPanel(this);
    control_ = new ControlPanel(this);
    curve_ = new CurvePanel(this);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(monitor_);
    // ControlPanel 较高（约 480px），放入滚动区防止在 ~420px 分栏高度下被裁剪。
    QScrollArea* ctrlScroll = new QScrollArea(this);
    ctrlScroll->setWidget(control_);
    ctrlScroll->setWidgetResizable(true);
    ctrlScroll->setFrameShape(QFrame::NoFrame);
    splitter->addWidget(ctrlScroll);
    splitter->setSizes({420, 380});

    QWidget* center = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(center);
    lay->addWidget(splitter, 3);
    lay->addWidget(curve_, 2);
    setCentralWidget(center);

    connect(worker_, &ControlWorker::telemetryUpdatedAll, monitor_, &MonitorPanel::onTelemetry);
    connect(worker_, &ControlWorker::telemetryUpdated, curve_, &CurvePanel::onTelemetry);
    connect(worker_, &ControlWorker::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(worker_, &ControlWorker::faultDetected, this, &MainWindow::onFaultDetected);
    connect(worker_, &ControlWorker::detectionMessage, this,
            [this](const QString& m) { statusBar()->showMessage(m); });
    connect(worker_, &ControlWorker::slavesDetected, this,
            [this](const QList<quint16>& slaves, quint16 active) {
                Q_UNUSED(active);
                monitor_->setSlaves(slaves);
            });

    connect(control_, &ControlPanel::enableRequested, worker_, &ControlWorker::enableRequested);
    connect(control_, &ControlPanel::disableRequested, worker_, &ControlWorker::disableRequested);
    connect(control_, &ControlPanel::quickStopRequested, worker_, &ControlWorker::quickStopRequested);
    connect(control_, &ControlPanel::faultResetRequested, worker_, &ControlWorker::faultResetRequested);
    connect(control_, &ControlPanel::operateModeChanged, worker_, &ControlWorker::setOperateModeRequested);
    connect(control_, &ControlPanel::targetRequested, worker_, &ControlWorker::setTargetRequested);

    statusBar()->showMessage(QStringLiteral("未连接"));

    QTimer::singleShot(0, this, &MainWindow::promptConnection);
}

MainWindow::~MainWindow()
{
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

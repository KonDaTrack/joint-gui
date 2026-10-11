#pragma once
#include <QMainWindow>
#include <QThread>
#include "core/ControlWorker.h"

class MonitorPanel;
class ControlPanel;
class CurvePanel;
class ControlServer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    // buildUi=false（默认的启动方式）：不建任何面板、不显示窗口，
    // 只把设备和 9002 服务跑起来。界面在上位机的浏览器里。
    // 传 true 才建本地窗口（对应 `--ui`，调试/现场排查用）。
    explicit MainWindow(bool buildUi = false, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void promptConnection();    // 有窗口时：弹连接框
    void autoConnect();         // 无窗口时：不弹框，直接自动检测并连上
    void onConnectionChanged(bool connected, QString busName, int slaveCount, QString error);
    void onFaultDetected(QString message);

private:
    void buildInterface();      // 本地窗口的全部构造（只有 --ui 才走到）


private:
    bool buildUi_ = false;  // false 时下面三个面板**全是 nullptr**，凡是用到它们
                            // 的地方都必须先看这个开关（接线已按此分组）
    QThread thread_;        // 设备线程（SDK 调用都在这里）
    QThread serverThread_;  // 网络线程（与设备/UI 隔离，服务端问题不得影响控制）
    ControlWorker* worker_ = nullptr;
    ControlServer* server_ = nullptr;
    MonitorPanel* monitor_ = nullptr;   // 以下三个仅 buildUi_ 时非空
    ControlPanel* control_ = nullptr;
    CurvePanel* curve_ = nullptr;
};

#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include "device/JointTypes.h"

class QWebSocketServer;
class QWebSocket;

// 上位机（远程客户端）接入服务：WebSocket + JSON。
//
// 跑在**独立线程**，与设备线程、UI 线程都隔离——
// 这是硬要求：服务端出任何问题（拥塞、崩溃、恶意客户端）都不能影响本地控制，
// 否则"下位机必须可靠"这条就不成立。
//
// P1 只支持单客户端（简化控制权模型）；多客户端留待后续。
class ControlServer : public QObject
{
    Q_OBJECT
public:
    explicit ControlServer(QObject* parent = nullptr);
    ~ControlServer() override;

public slots:
    void startServer(quint16 port);   // 在服务线程里创建监听（必须在本线程调用）
    void stopServer();

    // 以下槽由设备线程/UI 线程队列调用
    void onTelemetry(const QList<Joint::Telemetry>& list);
    // 参数须与 ControlWorker::connectionChanged 的前缀一致（信号参数多、槽可少，但不能错位）
    void onConnectionChanged(bool connected, const QString& busName, int slaveCount);
    void onSlavesDetected(const QList<quint16>& slaves, quint16 active);
    void onSlaveModels(const QStringList& shortNames, const QStringList& modelInfos);
    void onControlOwnerChanged(const QString& owner, const QString& reason);
    void onFault(const QString& message);

signals:
    // 上行命令转发给 ControlWorker（队列连接，天然跨线程）
    void requestControlReceived();
    void releaseControlReceived();
    void heartbeatReceived();
    void remoteCommandReceived(const QString& name, const QJsonObject& args);

    void serverLog(const QString& message);   // 供状态栏显示

private slots:
    void onNewConnection();
    void onTextMessage(const QString& message);
    void onClientDisconnected();

private:
    void send(const QJsonObject& obj);
    void sendTelemetryThrottled();

    QWebSocketServer* server_ = nullptr;
    QWebSocket* client_ = nullptr;   // P1 单客户端

    // 下行遥测缓存：设备线程 500Hz 推来，这里按 kTelemetryHz 节流后转发
    QList<Joint::Telemetry> lastTelemetry_;
    QElapsedTimer telemetryTimer_;
    QList<quint16> slaves_;
    QStringList shortNames_, modelInfos_;
    quint16 activeSlave_ = 0;
    bool connected_ = false;
    int slaveCount_ = 0;
    QString busName_;   // 总线类型（"EtherCAT"/"CANopen"/"仿真"）——界面须显著区分仿真与真机
    QString owner_ = QStringLiteral("local");
};

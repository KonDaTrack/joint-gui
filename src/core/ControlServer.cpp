#include "core/ControlServer.h"
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>

// 下行遥测频率。设备侧是 500Hz，远程客户端不需要这个频率：
// 浏览器渲染是 60Hz，传 500Hz 只是浪费带宽并让曲线抖。降频后反而更干净。
static const int kTelemetryHz = 50;

ControlServer::ControlServer(QObject* parent)
    : QObject(parent)
{
}

ControlServer::~ControlServer()
{
    stopServer();
}

void ControlServer::startServer(quint16 port)
{
    if (server_) return;
    server_ = new QWebSocketServer(QStringLiteral("joint-gui"),
                                   QWebSocketServer::NonSecureMode, this);
    if (!server_->listen(QHostAddress::Any, port)) {
        // 端口被占等失败：只报告，不影响本地运行
        emit serverLog(QStringLiteral("远程服务启动失败：%1").arg(server_->errorString()));
        delete server_;
        server_ = nullptr;
        return;
    }
    connect(server_, &QWebSocketServer::newConnection, this, &ControlServer::onNewConnection);
    emit serverLog(QStringLiteral("远程服务已启动，端口 %1").arg(port));
}

void ControlServer::stopServer()
{
    if (client_) {
        client_->close();
        client_ = nullptr;
    }
    if (server_) {
        server_->close();
        delete server_;
        server_ = nullptr;
    }
}

void ControlServer::onNewConnection()
{
    QWebSocket* sock = server_->nextPendingConnection();
    // P1 单客户端：已有连接就拒掉后来的，避免多个上位机同时争控制权
    if (client_) {
        sock->close();
        sock->deleteLater();
        return;
    }
    client_ = sock;
    connect(client_, &QWebSocket::textMessageReceived, this, &ControlServer::onTextMessage);
    connect(client_, &QWebSocket::disconnected, this, &ControlServer::onClientDisconnected);
    emit serverLog(QStringLiteral("上位机已接入"));

    // 握手：把当前从站/型号/控制权一次性告知，客户端不必等首帧遥测
    QJsonObject hello;
    hello[QStringLiteral("type")] = QStringLiteral("hello");
    hello[QStringLiteral("owner")] = owner_;
    hello[QStringLiteral("slaveCount")] = slaveCount_;
    // 仿真/真机必须让上位机一眼可辨——否则操作者可能把仿真数据当真
    hello[QStringLiteral("bus")] = busName_;
    hello[QStringLiteral("simulated")] = (busName_ == QStringLiteral("仿真"));
    QJsonArray arr;
    for (int i = 0; i < slaves_.size(); ++i) {
        QJsonObject s;
        s[QStringLiteral("slave")] = slaves_.at(i);
        s[QStringLiteral("shortName")] = (i < shortNames_.size()) ? shortNames_.at(i) : QString();
        s[QStringLiteral("model")] = (i < modelInfos_.size()) ? modelInfos_.at(i) : QString();
        s[QStringLiteral("active")] = (slaves_.at(i) == activeSlave_);
        arr.append(s);
    }
    hello[QStringLiteral("slaves")] = arr;
    send(hello);
}

void ControlServer::onClientDisconnected()
{
    if (client_) {
        client_->deleteLater();
        client_ = nullptr;
    }
    emit serverLog(QStringLiteral("上位机已断开"));
    // 客户端断开时若正持有控制权，立刻交还（不等心跳超时，更快更安全）
    emit releaseControlReceived();
}

void ControlServer::onTextMessage(const QString& message)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit serverLog(QStringLiteral("上位机消息解析失败：%1").arg(err.errorString()));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("requestControl")) {
        emit requestControlReceived();
    } else if (type == QLatin1String("releaseControl")) {
        emit releaseControlReceived();
    } else if (type == QLatin1String("heartbeat")) {
        emit heartbeatReceived();
    } else if (type == QLatin1String("command")) {
        emit remoteCommandReceived(obj.value(QStringLiteral("name")).toString(),
                                   obj.value(QStringLiteral("args")).toObject());
    }
}

void ControlServer::send(const QJsonObject& obj)
{
    if (!client_ || client_->state() != QAbstractSocket::ConnectedState) return;
    client_->sendTextMessage(QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void ControlServer::onTelemetry(const QList<Joint::Telemetry>& list)
{
    lastTelemetry_ = list;
    sendTelemetryThrottled();
}

void ControlServer::sendTelemetryThrottled()
{
    if (!client_) return;
    // 节流：不足一帧间隔就丢弃，避免 500Hz 全部灌给客户端
    if (telemetryTimer_.isValid() && telemetryTimer_.elapsed() < 1000 / kTelemetryHz) return;
    telemetryTimer_.restart();
    if (lastTelemetry_.isEmpty()) return;

    QJsonObject root;
    root[QStringLiteral("type")] = QStringLiteral("telemetry");
    root[QStringLiteral("ts")] = QDateTime::currentMSecsSinceEpoch();
    root[QStringLiteral("owner")] = owner_;
    root[QStringLiteral("activeSlave")] = activeSlave_;

    QJsonArray arr;
    for (const Joint::Telemetry& t : lastTelemetry_) {
        QJsonObject o;
        o[QStringLiteral("slave")] = t.slave;
        o[QStringLiteral("connected")] = t.connected;
        o[QStringLiteral("positionDeg")] = t.positionDeg;
        o[QStringLiteral("velocityDps")] = t.velocityDps;
        o[QStringLiteral("torqueNm")] = t.torqueNm;
        o[QStringLiteral("temperatureC")] = t.temperatureC;
        o[QStringLiteral("statusWord")] = t.statusWord;
        o[QStringLiteral("driveState")] = static_cast<int>(t.driveState);
        o[QStringLiteral("errorCode")] = t.errorCode;
        o[QStringLiteral("limitExceeded")] = t.limitExceeded;
        o[QStringLiteral("ratedTorqueNm")] = t.ratedTorqueNm;
        arr.append(o);
    }
    root[QStringLiteral("slaves")] = arr;
    send(root);
}

void ControlServer::onConnectionChanged(bool connected, const QString& busName, int slaveCount)
{
    connected_ = connected;
    slaveCount_ = slaveCount;
    busName_ = busName;
    if (!client_) return;
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("connection");
    o[QStringLiteral("connected")] = connected;
    o[QStringLiteral("bus")] = busName;
    o[QStringLiteral("slaveCount")] = slaveCount;
    send(o);
}

void ControlServer::onSlavesDetected(const QList<quint16>& slaves, quint16 active)
{
    slaves_ = slaves;
    activeSlave_ = active;
}

void ControlServer::onSlaveModels(const QStringList& shortNames, const QStringList& modelInfos)
{
    shortNames_ = shortNames;
    modelInfos_ = modelInfos;
}

void ControlServer::onControlOwnerChanged(const QString& owner, const QString& reason)
{
    owner_ = owner;
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("controlOwner");
    o[QStringLiteral("owner")] = owner;
    o[QStringLiteral("reason")] = reason;
    send(o);
}

void ControlServer::onFault(const QString& message)
{
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("fault");
    o[QStringLiteral("message")] = message;
    send(o);
}

void ControlServer::onLoadCommand(double torqueNm, double volt)
{
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("loadState");
    o[QStringLiteral("torqueNm")] = torqueNm;
    o[QStringLiteral("volt")] = volt;
    // 如实告诉客户端后端未接线：否则界面会显示"已下发"，而制动器毫无反应
    o[QStringLiteral("implemented")] = false;
    o[QStringLiteral("note")] = QStringLiteral("RS485 后端未接线");
    send(o);
}

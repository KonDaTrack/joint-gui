#include <QApplication>
#include "device/JointTypes.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<Joint::Telemetry>("Joint::Telemetry");
    qRegisterMetaType<Joint::TargetCommand>("Joint::TargetCommand");
    qRegisterMetaType<Joint::OperateMode>("Joint::OperateMode");
    qRegisterMetaType<AppConfig>("AppConfig");
    qRegisterMetaType<QList<Joint::Telemetry>>("QList<Joint::Telemetry>");
    qRegisterMetaType<QList<quint16>>("QList<quint16>");

    MainWindow w;
    w.show();
    return app.exec();
}

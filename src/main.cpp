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

    MainWindow w;
    w.show();
    return app.exec();
}

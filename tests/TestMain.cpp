#include <QtTest/QtTest>
#include <QCoreApplication>

// 各测试套件入口（在各自 .cpp 中定义，返回 QTest::qExec 的失败数）。
int runUnitConverterTests(int argc, char *argv[]);
int runJointModelTests(int argc, char *argv[]);
int runMitFrameCodecTests(int argc, char *argv[]);
int runDeviceParamsTests(int argc, char *argv[]);
int runLoadControllerTests(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    // 必须在 qExec 之前建好 QCoreApplication：异步测试要用 QTimer / QEventLoop，
    // 没有它会报 "QEventLoop: Cannot be used without QApplication" 并**直接卡死**
    // （不是失败，是挂住）。原来的测试都是纯函数，从不进事件循环，所以一直没暴露。
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runUnitConverterTests(argc, argv);
    status |= runJointModelTests(argc, argv);
    status |= runMitFrameCodecTests(argc, argv);
    status |= runDeviceParamsTests(argc, argv);
    status |= runLoadControllerTests(argc, argv);
    return status;
}

#include <QtTest/QtTest>

// 各测试套件入口（在各自 .cpp 中定义，返回 QTest::qExec 的失败数）。
int runUnitConverterTests(int argc, char *argv[]);
int runJointModelTests(int argc, char *argv[]);
int runMitFrameCodecTests(int argc, char *argv[]);
int runDeviceParamsTests(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    int status = 0;
    status |= runUnitConverterTests(argc, argv);
    status |= runJointModelTests(argc, argv);
    status |= runMitFrameCodecTests(argc, argv);
    status |= runDeviceParamsTests(argc, argv);
    return status;
}

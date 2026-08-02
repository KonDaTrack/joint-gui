#include <QtTest/QtTest>
#include "device/JointTypes.h"

class TestDeviceParams : public QObject
{
    Q_OBJECT
private slots:
    void validOnlyWhenAllSet();
    void invalidWhenAnyZero();
};

void TestDeviceParams::validOnlyWhenAllSet()
{
    Joint::DeviceParams p;
    p.encoderPulsesPerRev = 65536;
    p.gearRatio = 100.0;
    p.ratedTorqueNm = 2.0;
    QVERIFY(p.valid());
}

void TestDeviceParams::invalidWhenAnyZero()
{
    Joint::DeviceParams d;   // 默认全 0 = 未读到，应无效
    QVERIFY(!d.valid());
    Joint::DeviceParams a, b, c;
    a.encoderPulsesPerRev = 65536; a.gearRatio = 100.0;   // ratedTorqueNm 缺
    b.encoderPulsesPerRev = 65536; b.ratedTorqueNm = 2.0; // gearRatio 缺
    c.gearRatio = 100.0; c.ratedTorqueNm = 2.0;           // encoderPulsesPerRev 缺
    QVERIFY(!a.valid());
    QVERIFY(!b.valid());
    QVERIFY(!c.valid());
}

int runDeviceParamsTests(int argc, char *argv[]) { return QTest::qExec(new TestDeviceParams, argc, argv); }
#include "TestDeviceParams.moc"

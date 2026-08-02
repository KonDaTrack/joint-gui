#include <QtTest/QtTest>
#include "core/UnitConverter.h"

class TestUnitConverter : public QObject
{
    Q_OBJECT
private slots:
    void pulsesToDeg();
    void degToPulses();
    void permilleToNm();
    void radToDeg();
};

void TestUnitConverter::pulsesToDeg()
{
    // 65536 脉冲/圈，无减速比 → 16384 脉冲 = 90°
    QCOMPARE(UnitConverter::pulsesToDeg(16384, 65536, 1.0), 90.0);
    // 减速比 100：电机转 65536 脉冲，输出 1 圈 = 360°
    QCOMPARE(UnitConverter::pulsesToDeg(65536, 65536, 100.0), 3.6);
}

void TestUnitConverter::degToPulses()
{
    QCOMPARE(UnitConverter::degToPulses(90.0, 65536, 1.0), 16384.0);
    QCOMPARE(UnitConverter::degToPulses(360.0, 65536, 100.0), 6553600.0);
}

void TestUnitConverter::permilleToNm()
{
    // 额定 2 N·m，500‰ → 1 N·m
    QCOMPARE(UnitConverter::permilleToNm(500, 2.0), 1.0);
    QCOMPARE(UnitConverter::nmToPermille(1.0, 2.0), 500.0);
}

void TestUnitConverter::radToDeg()
{
    QVERIFY(qAbs(UnitConverter::radToDeg(3.14159265358979323846) - 180.0) < 1e-9);
    QVERIFY(qAbs(UnitConverter::degToRad(180.0) - 3.14159265358979323846) < 1e-9);
}

int runUnitConverterTests(int argc, char *argv[])
{
    TestUnitConverter t;
    return QTest::qExec(&t, argc, argv);
}
#include "TestUnitConverter.moc"

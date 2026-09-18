#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/LoadController.h"

// 标定与状态机的测试。异步部分用 JOINT_LOAD_TOOL 指向 /bin/true（恒成功）或
// 一个不存在的路径（FailedToStart），这样不需要真硬件就能验证成功/失败两条路。
class TestLoadController : public QObject
{
    Q_OBJECT
private slots:
    void calibration();
    void outOfRangeRejected();
    void writeSuccessReportsApplied();
    void writeFailureIsReported();
    void coalescingKeepsLastValue();
};

void TestLoadController::calibration()
{
    // 标定：50 N·m ↔ 10V，模块单位 mV（0~10000）
    QCOMPARE(LoadController::mvForNm(50.0), 10000);
    QCOMPARE(LoadController::mvForNm(0.0), 0);
    QCOMPARE(LoadController::mvForNm(20.0), 4000);   // 20 N·m → 4.000V
    QCOMPARE(LoadController::voltForNm(20.0), 4.0);
    QCOMPARE(LoadController::voltForNm(50.0), 10.0);
}

void TestLoadController::outOfRangeRejected()
{
    QVERIFY(!LoadController::outOfRange(0.0));
    QVERIFY(!LoadController::outOfRange(50.0));
    QVERIFY(LoadController::outOfRange(50.1));       // 超过 10V 写不进模块
    QVERIFY(LoadController::outOfRange(-1.0));       // 负值无意义
}

void TestLoadController::writeSuccessReportsApplied()
{
    qputenv("JOINT_LOAD_TOOL", "/bin/true");         // 恒成功、忽略参数
    LoadController c;
    QSignalSpy spy(&c, &LoadController::writeFinished);

    c.requestWrite(20.0);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 20.0);
    QCOMPARE(spy.at(0).at(1).toBool(), true);
    QCOMPARE(c.appliedNm(), 20.0);
    QVERIFY(!c.busy());
    qunsetenv("JOINT_LOAD_TOOL");
}

void TestLoadController::writeFailureIsReported()
{
    // 工具不存在 → FailedToStart。这条路径必须在 Qt5 里只被处理一次
    // （Crashed 会 errorOccurred + finished 双发），否则状态机会卡在 Writing。
    qputenv("JOINT_LOAD_TOOL", "/nonexistent/modbus_ao_xyz");
    LoadController c;
    QSignalSpy spy(&c, &LoadController::writeFinished);

    c.requestWrite(10.0);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QVERIFY(!spy.at(0).at(2).toString().isEmpty());   // note 里要有可展示的原因
    // 失败 = 外设实际状态未知，不能假装是 0
    QCOMPARE(c.appliedNm(), -1.0);
    QVERIFY(!c.busy());
    qunsetenv("JOINT_LOAD_TOOL");
}

void TestLoadController::coalescingKeepsLastValue()
{
    // 合并语义：连点多次只保留最后一个值（排队会让"急停写 0"排在"写 30"后面）
    qputenv("JOINT_LOAD_TOOL", "/bin/true");
    LoadController c;
    QSignalSpy spy(&c, &LoadController::writeFinished);

    c.requestWrite(10.0);
    c.requestWrite(20.0);
    c.requestWrite(30.0);

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0 && !c.busy(), 3000);
    // 最终确认写入的必须是最后一个请求
    QCOMPARE(c.appliedNm(), 30.0);
    qunsetenv("JOINT_LOAD_TOOL");
}

int runLoadControllerTests(int argc, char *argv[])
{
    TestLoadController t;
    return QTest::qExec(&t, argc, argv);
}
#include "TestLoadController.moc"

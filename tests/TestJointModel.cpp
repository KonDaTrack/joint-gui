#include <QtTest/QtTest>
#include "device/sim/JointModel.h"

class TestJointModel : public QObject
{
    Q_OBJECT
private slots:
    void velocityIntegratesToPosition();
    void disabledStaysStill();
    void faultStopsMotion();
    void faultResetRecovers();
};

void TestJointModel::velocityIntegratesToPosition()
{
    JointModel m;
    m.setCycleMs(10);
    m.reset(0.0);
    m.enable();
    Joint::TargetCommand t;
    t.hasVelocity = true;
    t.velocityDps = 10.0;   // 10 deg/s
    m.setTarget(t);
    Joint::Telemetry tel;
    for (int i = 0; i < 100; ++i) tel = m.step();   // 共 1 秒
    // 一阶响应逼近 10 deg/s，1 秒后应接近 10°，略小于 10
    QVERIFY(tel.positionDeg > 5.0);
    QVERIFY(tel.positionDeg < 10.1);
    QVERIFY(tel.driveState == Joint::DriveState::OperationEnabled);
}

void TestJointModel::disabledStaysStill()
{
    JointModel m;
    m.setCycleMs(10);
    m.reset(0.0);
    Joint::TargetCommand t;
    t.hasVelocity = true;
    t.velocityDps = 100.0;
    m.setTarget(t);
    auto tel = m.step();
    QCOMPARE(tel.positionDeg, 0.0);
    QCOMPARE(tel.velocityDps, 0.0);
}

void TestJointModel::faultStopsMotion()
{
    JointModel m;
    m.setCycleMs(10);
    m.reset(0.0);
    m.enable();
    m.injectFault();
    Joint::TargetCommand t;
    t.hasVelocity = true;
    t.velocityDps = 50.0;
    m.setTarget(t);
    auto tel = m.step();
    QCOMPARE(tel.driveState, Joint::DriveState::Fault);
    QVERIFY(tel.errorCode != 0);
    QCOMPARE(tel.velocityDps, 0.0);
}

void TestJointModel::faultResetRecovers()
{
    JointModel m;
    m.setCycleMs(10);
    m.reset(0.0);
    m.enable();
    m.injectFault();
    m.faultReset();
    auto tel = m.step();
    QVERIFY(tel.driveState != Joint::DriveState::Fault);
    QCOMPARE(tel.errorCode, (quint16)0);
}

int runJointModelTests(int argc, char *argv[])
{
    TestJointModel t;
    return QTest::qExec(&t, argc, argv);
}
#include "TestJointModel.moc"

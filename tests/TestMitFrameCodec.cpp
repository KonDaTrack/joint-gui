#include <QtTest/QtTest>
#include "device/canopen/MitFrameCodec.h"
#include <cmath>

class TestMitFrameCodec : public QObject
{
    Q_OBJECT
private slots:
    void roundtrip();
    void clampsToRange();
};

void TestMitFrameCodec::roundtrip()
{
    MitLimits l;
    l.pMin = -12.5f; l.pMax = 12.5f;
    l.vMin = -30.0f; l.vMax = 30.0f;
    l.kpMin = 0.0f;  l.kpMax = 500.0f;
    l.kdMin = 0.0f;  l.kdMax = 5.0f;
    l.tMin = -30.0f; l.tMax = 30.0f;
    MitFrameCodec c;
    c.setLimits(l);

    uint8_t f[8] = {0};
    QVERIFY(c.pack(1.0, 2.0, 3.0, 5.0, 2.0, f));

    unsigned int _pos = (f[0] << 8) | f[1];
    unsigned int _vel = (f[2] << 4) | (f[3] >> 4);
    unsigned int _kp  = ((f[3] & 0x0F) << 8) | f[4];
    unsigned int _kd  = (f[5] << 4) | (f[6] >> 4);
    unsigned int _tor = ((f[6] & 0x0F) << 8) | f[7];

    float pos = MitFrameCodec::uintToFloat(_pos, l.pMin, l.pMax, 16);
    float vel = MitFrameCodec::uintToFloat(_vel, l.vMin, l.vMax, 12);
    float kp  = MitFrameCodec::uintToFloat(_kp,  l.kpMin, l.kpMax, 12);
    float kd  = MitFrameCodec::uintToFloat(_kd,  l.kdMin, l.kdMax, 12);
    float tor = MitFrameCodec::uintToFloat(_tor, l.tMin, l.tMax, 12);

    QVERIFY(std::fabs(pos - 1.0) < 0.05);
    QVERIFY(std::fabs(vel - 2.0) < 0.1);
    QVERIFY(std::fabs(tor - 3.0) < 0.1);
    QVERIFY(std::fabs(kp - 5.0) < 0.5);
    QVERIFY(std::fabs(kd - 2.0) < 0.05);
}

void TestMitFrameCodec::clampsToRange()
{
    MitLimits l;
    l.pMin = -1.0f; l.pMax = 1.0f;
    l.vMin = -10.0f; l.vMax = 10.0f;
    l.kpMin = 0.0f; l.kpMax = 100.0f;
    l.kdMin = 0.0f; l.kdMax = 1.0f;
    l.tMin = -5.0f; l.tMax = 5.0f;
    MitFrameCodec c;
    c.setLimits(l);
    uint8_t f[8] = {0};
    // 超出范围的值应被钳制（pack 不崩溃，且落在范围内）
    QVERIFY(c.pack(100.0, -100.0, 50.0, 9999.0, -2.0, f));
    unsigned int _pos = (f[0] << 8) | f[1];
    float pos = MitFrameCodec::uintToFloat(_pos, l.pMin, l.pMax, 16);
    QVERIFY(pos >= -1.0f && pos <= 1.0f);
}

int runMitFrameCodecTests(int argc, char *argv[]) { return QTest::qExec(new TestMitFrameCodec, argc, argv); }
#include "TestMitFrameCodec.moc"

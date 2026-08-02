#pragma once
#include <cstdint>

struct MitLimits {
    float pMin = 0, pMax = 0, vMin = 0, vMax = 0;
    float kpMin = 0, kpMax = 0, kdMin = 0, kdMax = 0;
    float tMin = 0, tMax = 0;
};

// CANopen MIT 力位混合帧编解码：位置 16bit、速度/力矩/KP/KD 各 12bit，共 8 字节。
// 输入使用 rad / rad/s / N·m（与 SDK 示例一致）。
class MitFrameCodec
{
public:
    void setLimits(const MitLimits& l) { limits_ = l; }

    bool pack(double posRad, double velRadPerSec, double torqueNm,
              double kp, double kd, uint8_t out[8]) const;

    static int   floatToUint(float x, float xMin, float xMax, int bits);
    static float uintToFloat(int x, float xMin, float xMax, int bits);

private:
    MitLimits limits_;
};

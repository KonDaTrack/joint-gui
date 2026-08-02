#include "device/canopen/MitFrameCodec.h"
#include <algorithm>

int MitFrameCodec::floatToUint(float x, float xMin, float xMax, int bits)
{
    // 先判退化区间：xMin > xMax 时 std::clamp 前置条件不满足（UB）
    const float span = xMax - xMin;
    if (span <= 0.0f) return 0;
    x = std::clamp(x, xMin, xMax);
    return (int)((x - xMin) * ((1U << bits) - 1) / span);
}

float MitFrameCodec::uintToFloat(int x, float xMin, float xMax, int bits)
{
    const float span = xMax - xMin;
    if (span <= 0.0f) return xMin;
    return xMin + x * span / (float)((1 << bits) - 1);
}

bool MitFrameCodec::pack(double posRad, double velRadPerSec, double torqueNm,
                         double kp, double kd, uint8_t out[8]) const
{
    unsigned int _pos = (unsigned int)floatToUint((float)posRad, limits_.pMin, limits_.pMax, 16);
    unsigned int _vel = (unsigned int)floatToUint((float)velRadPerSec, limits_.vMin, limits_.vMax, 12);
    unsigned int _kp  = (unsigned int)floatToUint((float)kp, limits_.kpMin, limits_.kpMax, 12);
    unsigned int _kd  = (unsigned int)floatToUint((float)kd, limits_.kdMin, limits_.kdMax, 12);
    unsigned int _tor = (unsigned int)floatToUint((float)torqueNm, limits_.tMin, limits_.tMax, 12);

    out[0] = (_pos & 0xff00) >> 8;
    out[1] = _pos & 0xff;
    out[2] = (_vel & 0xff0) >> 4;
    out[3] = ((_vel & 0xf) << 4) | ((_kp & 0xf00) >> 8);
    out[4] = _kp & 0xff;
    out[5] = (_kd & 0xff0) >> 4;
    out[6] = ((_kd & 0xf) << 4) | ((_tor & 0xf00) >> 8);
    out[7] = _tor & 0xff;
    return true;
}

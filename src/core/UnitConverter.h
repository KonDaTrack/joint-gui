#pragma once

// UI 统一使用物理单位（deg / deg/s / N·m），此处集中与设备原生单位互转。
class UnitConverter
{
public:
    // EtherCAT：脉冲 ↔ 角度
    static double pulsesToDeg(double pulses, double pulsesPerRev, double gearRatio);
    static double degToPulses(double deg, double pulsesPerRev, double gearRatio);
    // EtherCAT 力矩：额定千分之 ↔ N·m
    static double permilleToNm(double permille, double ratedNm);
    static double nmToPermille(double nm, double ratedNm);
    // CANopen：rad ↔ deg
    static double radToDeg(double rad);
    static double degToRad(double deg);
};

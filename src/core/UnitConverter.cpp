#include "core/UnitConverter.h"

double UnitConverter::pulsesToDeg(double pulses, double pulsesPerRev, double gearRatio)
{
    return pulses / (pulsesPerRev * gearRatio) * 360.0;
}

double UnitConverter::degToPulses(double deg, double pulsesPerRev, double gearRatio)
{
    return deg / 360.0 * pulsesPerRev * gearRatio;
}

double UnitConverter::permilleToNm(double permille, double ratedNm)
{
    return permille * ratedNm / 1000.0;
}

double UnitConverter::nmToPermille(double nm, double ratedNm)
{
    return ratedNm > 0.0 ? nm * 1000.0 / ratedNm : 0.0;
}

double UnitConverter::radToDeg(double rad)
{
    return rad * 180.0 / 3.14159265358979323846;
}

double UnitConverter::degToRad(double deg)
{
    return deg * 3.14159265358979323846 / 180.0;
}

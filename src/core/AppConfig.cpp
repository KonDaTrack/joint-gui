#include "core/AppConfig.h"

int AppConfig::controlCycleMs() const
{
    switch (busType) {
    case Joint::BusType::EtherCat: return ethCycleMs;
    case Joint::BusType::CanOpen:  return 4;   // MIT 插补周期
    case Joint::BusType::Simulation: return 10;
    }
    return 10;
}

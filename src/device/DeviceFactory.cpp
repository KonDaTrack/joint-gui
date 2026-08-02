#include "device/DeviceFactory.h"
#include "device/sim/SimulatedDevice.h"

std::unique_ptr<JointDevice> createDevice(Joint::BusType type)
{
    switch (type) {
    case Joint::BusType::EtherCat:
    case Joint::BusType::CanOpen:
        return nullptr;   // Task 15/16 接入
    case Joint::BusType::Simulation:
        return std::make_unique<SimulatedDevice>();
    }
    return nullptr;
}

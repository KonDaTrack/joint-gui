#include "device/DeviceFactory.h"
#include "device/sim/SimulatedDevice.h"
#include "device/ethercat/EthercatDevice.h"

std::unique_ptr<JointDevice> createDevice(Joint::BusType type)
{
    switch (type) {
    case Joint::BusType::EtherCat:
        return std::make_unique<EthercatDevice>();
    case Joint::BusType::CanOpen:
        return nullptr;   // Task 16 启用
    case Joint::BusType::Simulation:
        return std::make_unique<SimulatedDevice>();
    }
    return nullptr;
}

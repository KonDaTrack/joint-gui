#include "device/DeviceFactory.h"
#include "device/sim/SimulatedDevice.h"
#include "device/ethercat/EthercatDevice.h"
#include "device/canopen/CanopenDevice.h"

std::unique_ptr<JointDevice> createDevice(Joint::BusType type)
{
    switch (type) {
    case Joint::BusType::EtherCat:
        return std::make_unique<EthercatDevice>();
    case Joint::BusType::CanOpen:
        return std::make_unique<CanopenDevice>();
    case Joint::BusType::Simulation:
        return std::make_unique<SimulatedDevice>();
    }
    return nullptr;
}

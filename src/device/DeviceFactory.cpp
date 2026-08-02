#include "device/DeviceFactory.h"

std::unique_ptr<JointDevice> createDevice(Joint::BusType type)
{
    switch (type) {
    case Joint::BusType::EtherCat:
    case Joint::BusType::CanOpen:
    case Joint::BusType::Simulation:
        break;   // 后续任务接入各自实现
    }
    return nullptr;
}

#pragma once
#include "device/JointDevice.h"
#include <memory>

// 根据总线类型创建设备实例。EtherCAT/CANopen 实现见对应目录，仿真实现见 sim/。
std::unique_ptr<JointDevice> createDevice(Joint::BusType type);

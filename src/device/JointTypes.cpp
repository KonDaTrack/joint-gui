#include "device/JointTypes.h"

namespace Joint {

QString busTypeName(BusType t)
{
    switch (t) {
    case BusType::Auto:       return QStringLiteral("自动检测");
    case BusType::EtherCat:   return QStringLiteral("EtherCAT");
    case BusType::CanOpen:    return QStringLiteral("CANopen");
    case BusType::Simulation: return QStringLiteral("仿真");
    }
    return QStringLiteral("未知");
}

DriveState mapDriveState(quint16 sw)
{
    if ((sw & 0x08) == 0x08) return DriveState::Fault;              // bit3 故障
    const quint16 s = sw & 0x6F;                                    // bit0-2 + bit5 + bit6
    if (s == 0x40) return DriveState::SwitchOnDisabled;             // bit6
    if (s == 0x21) return DriveState::ReadyToSwitchOn;              // bit0+bit5
    if (s == 0x23) return DriveState::SwitchedOn;                   // bit0+bit1+bit5
    if (s == 0x27) return DriveState::OperationEnabled;             // bit0+bit1+bit2+bit5
    if (s == 0x07) return DriveState::QuickStopActive;              // bit0+bit1+bit2，bit5=0
    if ((sw & 0x0F) == 0x0F) return DriveState::FaultReactionActive;
    return DriveState::NotReadyToSwitchOn;
}

} // namespace Joint

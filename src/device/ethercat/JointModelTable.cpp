#include "device/ethercat/JointModelTable.h"

namespace JointModelTable {

namespace {
// 型号表。key = 驱动器 0x6076 原始值（实测）。
// 减速比来自设计文档表 2-1（90mm 用实物确认的 101，文档写 100）。
// 新批次固件若改了 0x6076，会匹配不上 → 回退手填值并告警，把读到的值加进本表即可。
const ModelInfo kModels[] = {
    { true, QStringLiteral("PHU-14H-70-F-B"),  QStringLiteral("70mm"),   9.6, 100.0,  250 },
    { true, QStringLiteral("PHU-20H-90-F-B"),  QStringLiteral("90mm"),  50.0, 101.0,  850 },
    { true, QStringLiteral("PHU-25H-110-F-B"), QStringLiteral("110mm"), 84.0, 110.0, 1600 },
};
} // namespace

ModelInfo byRatedTorqueKey(quint16 key6076)
{
    for (const ModelInfo& m : kModels) {
        if (m.key6076 == key6076) return m;
    }
    ModelInfo miss;
    miss.found = false;
    miss.key6076 = key6076;
    return miss;
}

} // namespace JointModelTable

#pragma once
#include <QString>

// 关节型号表：用驱动器 0x6076（额定）的原始值识别关节型号。
//
// 实测三个型号的 0x6076 各不相同且随额定力矩单调递增，可作"型号指纹"：
//   250 → 70mm / 9.6 N·m ；850 → 90mm / 50 N·m ；1600 → 110mm / 84 N·m
//
// 注意 0x6076 不是物理力矩（250/9.6=26、850/50=17、1600/84=19，比例不恒定），
// 所以不能反推额定值——只能用于识别，识别后取本表的规格值。
// 辅助指纹（0x6075 额定电流、0x6080 最大转速）也各不相同，可用于交叉核对。
namespace JointModelTable {

struct ModelInfo {
    bool found = false;      // 是否匹配到已知型号
    QString name;            // 完整型号名（未匹配时为空）
    QString shortName;       // 短名（如 "70mm"），用于标签页/下拉，够短又能区分
    double ratedNm = 0.0;    // 额定力矩 N·m
    double gearRatio = 0.0;  // 减速比
    quint16 key6076 = 0;     // 实际读到的 0x6076 原始值（未匹配时也填，便于加表）
};

// 按 0x6076 原始值查型号；找不到返回 found=false
ModelInfo byRatedTorqueKey(quint16 key6076);

} // namespace JointModelTable

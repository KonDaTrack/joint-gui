// eth_probe.cpp — EtherCAT 只读体检
//
// 目的：把「总线上有没有从站 / 从站能不能起来」和「起来了但读不到参数」这两截分开。
//       拆装电机后出现「识别不出型号 + 不能使能」，先跑这个定位到哪一截。
//
// 编译（在板上）：
//   g++ -O2 -o eth_probe eth_probe.cpp
//       -I<SDK>/include -L<SDK>/lib -leu_ethercat -lsoem -lcyhcs_log -lpthread
//   例：SDK=~/EYou/eyou_sdk/eyou_ethercat_sdk/eyou_ethercat_phu_sdk_aarch64_linux_gnu_xxx
//
// 运行：
//   sudo LD_LIBRARY_PATH=<SDK>/lib ./eth_probe <网卡名>
//   例：sudo LD_LIBRARY_PATH=.../lib ./eth_probe enx00e0bc4915ec
//
// 可选：
//   --skip-pdo <id>   跳过该从站的 PDO 配置再试一次（怀疑 PDO/SM 不匹配时用）
//   --sdo-scan        逐个试读一批 OD（慢，但能看出邮箱通不通、哪里开始失败）
//
// ★ 跑之前必须先停掉 joint-gui —— 一块网卡只能有一个 EtherCAT 主站，
//   两个主站抢同一张网卡，后起的那个必然 statecheck 失败。
//
// ★ 默认只读：不发使能、不发运动。加 --enable 才会试使能（危险，见文件末尾）。

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>          // sleep / usleep / system
#include "eu_ethercat.h"

// ---------------------------------------------------------------- 小工具
static const char *init_ret(int r)
{
    switch (r) {
    case ETH_SUCCESS:           return "ETH_SUCCESS (0)";
    case ETH_FAILED_INIT:       return "ETH_FAILED_INIT (1)";
    case ETH_FAILED_NOSLAVE:    return "ETH_FAILED_NOSLAVE (2)";
    case ETH_FAILED_CHECKSTATE: return "ETH_FAILED_CHECKSTATE (3)";
    default:                    return "(未知)";
    }
}

static const char *al_state(eth_State s)
{
    switch (s) {
    case eth_State_Init:        return "INIT";
    case eth_State_Pre_OP:      return "PRE-OP";
    case eth_State_Boot:        return "BOOT";
    case eth_State_Safe_OP:     return "SAFE-OP";
    case eth_State_Operational: return "OP";
    default:                    return "(?)";
    }
}

// CiA402 标准故障码里最常见的几个
static const char *fault_text(unsigned e)
{
    switch (e) {
    case 0x0000: return "";
    case 0x2310: return "过流";
    case 0x3210: return "母线过压";
    case 0x3220: return "母线欠压";
    case 0x4210: return "驱动器过温";
    case 0x4310: return "电机过温 / 热敏开路";
    case 0x7300: return "编码器/传感器错误";
    case 0x7305: return "编码器/传感器错误";
    case 0x7500: return "通信错误";
    case 0x8110: return "位置跟随误差";
    case 0x8611: return "位置超差";
    default:     return (e >= 0xFF00) ? "厂商自定义故障" : "";
    }
}

static void show_statusword(unsigned sw)
{
    printf("    状态字 0x6041 = 0x%04X\n", sw);
    printf("      [%d] Ready to switch on\n", (sw >> 0) & 1);
    printf("      [%d] Switched on\n", (sw >> 1) & 1);
    printf("      [%d] Operation enabled  ← 使能了没看这个\n", (sw >> 2) & 1);
    printf("      [%d] Fault               ← 1 = 有故障，读 0x603F\n", (sw >> 3) & 1);
    printf("      [%d] Voltage enabled\n", (sw >> 4) & 1);
    printf("      [%d] Quick stop\n", (sw >> 5) & 1);
    printf("      [%d] Switch on disabled\n", (sw >> 6) & 1);
    printf("      [%d] Warning\n", (sw >> 7) & 1);
    printf("      [%d] Target reached\n", (sw >> 10) & 1);
    printf("      [%d] Internal limit active\n", (sw >> 11) & 1);
}

// 读一个 uint32 的 SDO，成功返回 true
static bool sdo_u32(unsigned slave, unsigned index, unsigned sub, const char *what)
{
    huint32 v = 0;
    int r = eth_readSDO(slave, index, sub, &v, eth_DataType_uint32, 2000);
    if (r == ETH_SUCCESS)
        printf("    0x%04X:%02X  %-22s = %u\n", index, sub, what, v);
    else
        printf("    0x%04X:%02X  %-22s = 读失败 (ret=%d)\n", index, sub, what, r);
    return r == ETH_SUCCESS;
}

// -------------------------------------------------------------------- main
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: %s <EtherCAT网卡名> [--skip-pdo <id>] [--sdo-scan] [--enable]\n", argv[0]);
        printf("先跑 `ip -br link` 和 `ethtool <网卡>` 确认名字和 Link 状态。\n");
        return 2;
    }
    const char *ifname = argv[1];
    int skip_id = -1, do_sdo_scan = 0, do_enable = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--skip-pdo") && i + 1 < argc) skip_id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sdo-scan")) do_sdo_scan = 1;
        else if (!strcmp(argv[i], "--enable")) do_enable = 1;
    }

    printf("==================== 0. 网卡信息 ====================\n");
    printf("网卡名: %s\n", ifname);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip -br link show %s 2>&1", ifname);
    if (system(cmd) != 0) { /* ip 的返回码不用管，输出在上面 */ }
    snprintf(cmd, sizeof(cmd), "ethtool %s 2>&1 | grep -E 'Link detected|Speed|Duplex'", ifname);
    system(cmd);

    if (skip_id > 0) {
        int ids[1] = {skip_id};
        int n = eth_setSkipPdoConfigSlaves(ids, 1);
        printf("\n[已设置] 跳过从站 %d 的 PDO 配置（返回 %d）\n", skip_id, n);
    }

    printf("\n============ 1. 打开网卡、拉从站到 OP ============\n");
    int slaveCnt = -1;
    int r = eth_initDLL(ifname, 1, &slaveCnt);
    printf("eth_initDLL 返回 %d  %s\n", r, init_ret(r));
    printf("slaveCnt = %d\n", slaveCnt);

    if (r != ETH_SUCCESS) {
        printf("\n================= 分诊 =================\n");
        if (r == ETH_FAILED_INIT)
            printf("  ETH_FAILED_INIT：网卡根本没打开。\n"
                   "   → `ip -br link` 里没有这个名字？网卡名变了（USB 网卡换个 USB 口就会改名）。\n"
                   "   → 有名字但 `Link detected: no`？网线/对端没上电。\n"
                   "   → 有名字也有 Link，但被别的进程占着（joint-gui 还在跑？NetworkManager？）。\n");
        else if (r == ETH_FAILED_NOSLAVE)
            printf("  ETH_FAILED_NOSLAVE：网卡开了，但总线上一个从站都没应答。\n"
                   "   → 网线插的是从站的 IN 口吗（插 OUT 口经常扫不到）？\n"
                   "   → 从站（驱动器）真的上电了吗？它的 EtherCAT 灯闪吗？\n"
                   "   → 120Ω 终端电阻还在吗（拆装时最容易丢掉/碰掉）？\n"
                   "   → 换根网线、直接在 PC 上用 EYouServoStudio 单机连试试。\n");
        else
            printf("  ETH_FAILED_CHECKSTATE：从站扫到了，但它进不了 SAFE-OP。\n"
                   "   → 这一档不是网线问题，是从站的 ESM/配置/内部状态问题。\n"
                   "   → 先试：%s %s --skip-pdo 1   （跳过 PDO 配置，看能不能起来）\n"
                   "   → 还能起来说明是 PDO/SM 配置对不上；起不来则是从站内部（邮箱/PDI）问题。\n",
                   argv[0], ifname);
        printf("========================================\n");
        eth_freeDLL();
        return 1;
    }

    printf("\n================= 总线通了 =================\n");

    printf("\n============ 2. 逐从站读状态（只读）============\n");
    for (int s = 1; s <= slaveCnt; ++s) {
        eth_State st = eth_State_None;
        huint16 sw = 0, err = 0;
        hint32 temp = 0, pos = 0;
        hint16 tor = 0;
        eth_OperateMode mode = eth_OperateMode_Reserve;

        eth_getSlaveState(s, &st);
        eth_getStatusWord(s, &sw);
        eth_getErrorCode(s, &err);
        eth_getDriveTemper(s, &temp);
        eth_getActualPosition(s, &pos);
        eth_getActualTorque(s, &tor);
        eth_getOperateMode(s, &mode);

        printf("\n----- 从站 %d -----\n", s);
        printf("    AL 状态 : %s\n", al_state(st));
        show_statusword(sw);
        printf("    故障码 0x603F = 0x%04X  %s\n", err, fault_text(err));
        printf("    驱动器温度 = %d\n", temp);
        printf("    当前位置   = %d\n", pos);
        printf("    当前力矩   = %d\n", tor);
        printf("    当前模式   = %d\n", (int)mode);

        printf("\n    身份 / 型号相关 OD：\n");
        sdo_u32(s, 0x1000, 0x00, "设备类型 Device Type");
        sdo_u32(s, 0x1018, 0x01, "Vendor ID");
        sdo_u32(s, 0x1018, 0x02, "Product Code");
        sdo_u32(s, 0x1018, 0x03, "Revision");
        sdo_u32(s, 0x1018, 0x04, "Serial Number");
        printf("    --- 型号识别就靠下面这个（joint-gui 的指纹）---\n");
        sdo_u32(s, 0x6076, 0x00, "电机额定力矩 0x6076");
        sdo_u32(s, 0x6072, 0x00, "电机最大力矩 0x6072");
        sdo_u32(s, 0x6080, 0x00, "电机最高转速 0x6080");
        sdo_u32(s, 0x6075, 0x00, "电机额定电流 0x6075");
        sdo_u32(s, 0x608F, 0x01, "编码器分辨率分子");
        sdo_u32(s, 0x608F, 0x02, "编码器分辨率分母");
        sdo_u32(s, 0x603F, 0x00, "故障码(再读一次)");
    }

    if (do_sdo_scan) {
        printf("\n============ 3. SDO 扫一遍常用 OD ============\n");
        const unsigned idx[] = {0x6041, 0x6061, 0x6064, 0x606C, 0x6077, 0x6079,
                                0x605A, 0x605D, 0x6098, 0x6502};
        for (unsigned i = 0; i < sizeof(idx) / sizeof(idx[0]); ++i) {
            char nm[32];
            snprintf(nm, sizeof(nm), "OD 0x%04X", idx[i]);
            sdo_u32(1, idx[i], 0x00, nm);
        }
    }

    if (do_enable) {
        printf("\n============ 4. 试使能（危险）============\n");
        printf("!! 关节会立刻进入力矩控制。确认机械上脱开负载或有人扶住，手放在电源开关上 !!\n");
        printf("3 秒后开始...\n");
        sleep(3);
        eth_setOperateMode(1, eth_OperateMode_ProfileTorque);
        huint16 sw = 0;
        const unsigned cw[] = {0x0006, 0x0007, 0x000F};   // Shutdown -> Switch On -> Enable
        const char *nm[] = {"Shutdown(0x06)", "Switch On(0x07)", "Enable(0x0F)"};
        for (int i = 0; i < 3; ++i) {
            eth_setControlWord(1, cw[i]);
            usleep(300000);
            eth_getStatusWord(1, &sw);
            printf("  写 %-16s -> 状态字 0x%04X (fault=%d, enabled=%d)\n",
                   nm[i], sw, (sw >> 3) & 1, (sw >> 2) & 1);
            if ((sw >> 3) & 1) {
                huint16 e = 0;
                eth_getErrorCode(1, &e);
                printf("  ✗ 进故障了，0x603F = 0x%04X %s\n", e, fault_text(e));
                break;
            }
        }
        eth_setControlWord(1, 0x0000);
    }

    eth_freeDLL();
    return 0;
}

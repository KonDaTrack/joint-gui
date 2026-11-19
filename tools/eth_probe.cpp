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
//   例：./eth_probe enx00e04c3a41c0
//
// 可选：
//   --cycle <ms>      主站周期，默认 2（与 tools/read_joint_params.cpp 一致）
//   --force           init 失败也继续读 —— 从站到过 SAFE-OP 的话邮箱可能仍然通
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
    case 0x8611: return "E302 位置偏差过大";
    case 0x8400: return "E303 速度偏差过大";
    case 0x8401: return "E304 过速";
    case 0x7512: return "E213 转矩传感器通讯异常";
    case 0x5454: return "E301 STO 驱动禁止输入";
    case 0xFF30: return "E403 EtherCAT 网线未接（锁存）";
    case 0xFF31: return "E401 EtherCAT 丢包";
    case 0xFF32: return "E402 EtherCAT 未进 OP";
    case 0xFF20: return "E323 紧急停机";
    case 0xFF33: return "E405 PDO 配置项超限";
    case 0xFF34: return "E404 插补周期不支持";
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

// 抱闸相关的一组读数。手册里没有"抱闸已松开"的状态位，只能靠这几个间接看：
//   0x2111 在自动模式(0x2110=1)下由驱动器写，读回来可能反映它的实际输出
//   0x60FD 数字量输入 —— 如果抱闸有反馈触点接在 DI 上，这里能看到翻转
// 使能前后各读一次，对比着看。
static void brake_snapshot(const char *when)
{
    printf("  --- 抱闸相关读数（%s）---\n", when);
    sdo_u32(1, 0x2110, 0x00, "Brake Mode (0=手动 1=跟随使能)");
    sdo_u32(1, 0x2111, 0x00, "Brake Output (0=松开 1=闭合)");
    sdo_u32(1, 0x2112, 0x00, "Brake Test Mode");
    sdo_u32(1, 0x2113, 0x00, "Release Time");
    sdo_u32(1, 0x2114, 0x00, "Engage Time");
    sdo_u32(1, 0x2115, 0x00, "Safe Speed");
    sdo_u32(1, 0x60FD, 0x00, "Digital Inputs 0x60FD");
}

// -------------------------------------------------------------------- main
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: %s <EtherCAT网卡名> [选项]\n", argv[0]);
        printf("  --cycle <ms>      主站周期，默认 2（与 tools/read_joint_params.cpp 一致）\n");
        printf("  --force           init 失败也继续读 —— 从站到过 SAFE-OP 的话邮箱可能是通的\n");
        printf("  --skip-pdo <id>   跳过该从站的 PDO 配置\n");
        printf("  --sdo-scan        多扫一批常用 OD\n");
        printf("  --fault-reset     写 0x6040 清驱动器故障（CiA402 Fault Reset）\n");
        printf("  --claim-bus       把控制权 0x2100 切成 1(EtherCAT) —— 串口上位机调完\n"
               "                    参数后忘了切回来，主站就永远进不了 OP\n");
        printf("  --brake-test      切手动模式直接命令抱闸（0x2111），听咔哒声判断抱闸好坏\n");
        printf("  --enable          试使能（危险，默认不发）\n");
        printf("先跑 `ip -br link` 和 `ethtool <网卡>` 确认名字和 Link 状态。\n");
        return 2;
    }
    const char *ifname = argv[1];
    int skip_id = -1, do_sdo_scan = 0, do_enable = 0, force = 0, cycle_ms = 2,
        do_freset = 0, do_claim = 0, do_brake = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--skip-pdo") && i + 1 < argc) skip_id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cycle") && i + 1 < argc) cycle_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sdo-scan")) do_sdo_scan = 1;
        else if (!strcmp(argv[i], "--enable")) do_enable = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--fault-reset")) do_freset = 1;
        else if (!strcmp(argv[i], "--claim-bus")) do_claim = 1;
        else if (!strcmp(argv[i], "--brake-test")) do_brake = 1;
        else {
            printf("!! 未知选项：'%s'（尾随字符也算，比如 '--enable~'）\n", argv[i]);
            printf("   不带参数跑一次看用法。\n");
            return 2;
        }
    }

    printf("==================== 0. 网卡信息 ====================\n");
    printf("网卡名: %s\n", ifname);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip -br link show %s 2>&1", ifname);
    if (system(cmd) != 0) { /* ip 的返回码不用管，输出在上面 */ }
    snprintf(cmd, sizeof(cmd), "ethtool %s 2>&1 | grep -E 'Link detected|Speed|Duplex'", ifname);
    if (system(cmd) != 0) { /* grep 没匹配到会返回 1，不是错误 */ }

    if (skip_id > 0) {
        int ids[1] = {skip_id};
        int n = eth_setSkipPdoConfigSlaves(ids, 1);
        printf("\n[已设置] 跳过从站 %d 的 PDO 配置（返回 %d）\n", skip_id, n);
    }

    printf("\n============ 1. 打开网卡、拉从站到 OP ============\n");
    printf("主站周期: %d ms\n", cycle_ms);
    int slaveCnt = -1;
    int r = eth_initDLL(ifname, cycle_ms, &slaveCnt);
    printf("eth_initDLL 返回 %d  %s\n", r, init_ret(r));
    printf("slaveCnt = %d\n", slaveCnt);

    if (r != ETH_SUCCESS && !force) {
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
            printf("  ETH_FAILED_CHECKSTATE：从站扫到了，但进不了目标状态。\n"
                   "   注意日志里是 safe_op 还是 op —— 差一级，意义完全不同：\n"
                   "     · safe_op 失败 → 邮箱/SM/PDI 这一层就没通\n"
                   "     · op 失败      → 已经到过 SAFE-OP，卡在最后一跳\n"
                   "   → 先试别的周期：%s %s --cycle 2 / --cycle 4\n"
                   "   → 再试：        %s %s --skip-pdo 1\n"
                   "   → 都起不来就硬读：%s %s --force   （从站到过 SAFE-OP，邮箱可能仍然通）\n",
                   argv[0], ifname, argv[0], ifname, argv[0], ifname);
        printf("========================================\n");
        eth_freeDLL();
        return 1;
    }
    if (r != ETH_SUCCESS)
        printf("\n[--force] init 失败了，但从站到过 SAFE-OP —— 邮箱可能还通，硬着头皮往下读。\n");
    else
        printf("\n================= 总线通了 =================\n");

    if (slaveCnt < 1) {
        printf("slaveCnt = %d，没有从站可读，退出。\n", slaveCnt);
        eth_freeDLL();
        return 1;
    }

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

        // ★ 最先看这个：控制权不在 EtherCAT 上，主站就永远拉不到 OP，
        //   而 SDO / 邮箱照常通 —— 症状是"从站扫得到、参数读得出、就是进不了 OP"。
        //   ServoStudio 里"控制权选择 PC"就是把它写成 0(UART)，
        //   "总线配置 → 控制权切换成 EtherCAT"就是写 1。
        printf("\n    ★ 控制权 0x2100（不在 EtherCAT 上就进不了 OP）：\n");
        sdo_u32(s, 0x2100, 0x00, "0x2100 控制权 (0=UART/串口  1=EtherCAT  2=CANopen)");

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

        // 锁存的故障/报警历史（0x2541 / 0x2540 是数组对象，:00 是条目数）。
        // 0x603F 只反映"当前"故障；如果它归零了但问题还在，历史里可能留着线索。
        huint32 nf = 0, nw = 0;
        eth_readSDO(s, 0x2541, 0x00, &nf, eth_DataType_uint32, 800);
        eth_readSDO(s, 0x2540, 0x00, &nw, eth_DataType_uint32, 800);
        printf("\n    锁存故障历史 0x2541（:00 = %u 条）:\n", (unsigned)nf);
        for (huint8 i = 1; i <= 8; ++i) {
            huint32 v = 0;
            if (eth_readSDO(s, 0x2541, i, &v, eth_DataType_uint32, 800) != ETH_SUCCESS) break;
            if (v == 0) continue;
            printf("      0x2541:%02u = %u (0x%04X) %s\n", (unsigned)i, v, v, fault_text((unsigned)v));
        }
        printf("    锁存报警历史 0x2540:\n");
        for (huint8 i = 1; i <= 8; ++i) {
            huint32 v = 0;
            if (eth_readSDO(s, 0x2540, i, &v, eth_DataType_uint32, 800) != ETH_SUCCESS) break;
            if (v == 0) continue;
            printf("      0x2540:%02u = %u (0x%04X)\n", (unsigned)i, v, v);
        }
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

    // ---- 故障复位：CiA402 控制字 bit7 上升沿（0x6040: 0x0000 -> 0x0080 -> 0x0000）
    // 现在没进 OP，PDO 不跑，所以 SDO 写 0x6040 不会被周期覆盖，能生效。
    // ---- 故障复位：CiA402 控制字 bit7 上升沿
    // 必须走 PDO 缓冲（eth_setControlWord）。0x6040 在 RxPDO 里，到了 OP 之后
    // 主站每周期发的过程数据会把 SDO 写进去的值覆盖回去 —— 这条实测踩过。
    if (do_freset) {
        printf("\n============ 4. 故障复位 ============\n");
        huint16 sw = 0;
        eth_getStatusWord(1, &sw);
        printf("  复位前 状态字 0x%04X  状态 0x%02X  bit3(故障)=%d\n", sw, sw & 0x6F, (sw >> 3) & 1);

        eth_setControlWord(1, 0x0000);  usleep(60000);
        eth_setControlWord(1, 0x0080);  usleep(60000);   // bit7 上升沿 = Fault Reset
        eth_setControlWord(1, 0x0000);  usleep(250000);

        eth_getStatusWord(1, &sw);
        printf("  复位后 状态字 0x%04X  状态 0x%02X  bit3(故障)=%d\n", sw, sw & 0x6F, (sw >> 3) & 1);

        huint32 e = 0;
        if (eth_readSDO(1, 0x603F, 0x00, &e, eth_DataType_uint32, 2000) == ETH_SUCCESS)
            printf("  再读 0x603F = %u (0x%04X) %s\n", e, e, fault_text((unsigned)e));
        if ((sw >> 3) & 1)
            printf("  ✗ 还在故障态。若 0x603F 仍是 0xFF30 (E403 EtherCAT网线未接)：\n"
                   "     网线插牢/换根线，再断电重启一次试试\n");
        else
            printf("  ✓ 故障态已解除 —— 接着跑 --enable 看能不能使能\n");
    }

    // ---- 抱闸手动测试：切到手动模式直接命令抱闸输出。
    // 自动模式(0x2110=1)下是驱动器自己控抱闸，光看 0x2111 分不清是
    // "驱动器没去松开" 还是 "抱闸回路没电/断线"。手动模式直接把它拉开，听声音。
    if (do_brake) {
        printf("\n============ 7. 抱闸手动测试 ============\n");
        printf("不管当前 0x2110 是几，先切成 0(手动)，然后直接命令 0x2111。\n");
        printf("听咔哒声 —— 有 = 抱闸本体和线路都好；没有 = 回路没电/断线/线圈坏。\n\n");

        huint8 v = 0;
        eth_readSDO(1, 0x2110, 0x00, &v, eth_DataType_uint8, 2000);
        printf("  当前 0x2110 = %u (0=手动 1=自动)\n", (unsigned)v);
        eth_readSDO(1, 0x2111, 0x00, &v, eth_DataType_uint8, 2000);
        printf("  当前 0x2111 = %u (0=松开 1=闭合)\n\n", (unsigned)v);

        huint8 zero = 0, one = 1;
        eth_writeSDO(1, 0x2110, 0x00, &zero, eth_DataType_uint8, 2000);
        usleep(200000);
        printf("  → 写 0x2111 = 0（松开抱闸）  ……听！\n");
        eth_writeSDO(1, 0x2111, 0x00, &zero, eth_DataType_uint8, 2000);
        sleep(4);

        printf("\n  → 写 0x2111 = 1（闭合抱闸）  ……再听！\n");
        eth_writeSDO(1, 0x2111, 0x00, &one, eth_DataType_uint8, 2000);
        sleep(3);

        printf("\n  → 恢复 0x2110 = 1（自动跟随使能）\n");
        eth_writeSDO(1, 0x2110, 0x00, &one, eth_DataType_uint8, 2000);

        printf("\n  判断：\n");
        printf("    两次都有咔哒声 → 抱闸好，问题在自动模式的触发条件\n");
        printf("    只有一次 / 都没有  → 抱闸回路供电或线断了，去量抱闸线\n");
        printf("    还可以用手试：手动松开(0x2111=0)后输出端应该能转动\n");
    }

    // ---- 抢占控制权：把 0x2100 写成 1(EtherCAT)。
    // 控制权在 UART(0) 时主站拉不到 OP —— 用串口上位机调完参数后会留在 0，
    // 忘了切回来就会出现"从站扫得到、SDO 读得出、就是进不了 OP"。
    if (do_claim) {
        printf("\n============ 6. 抢占总线控制权（0x2100）============\n");
        huint8 v = 0;
        int rr = eth_readSDO(1, 0x2100, 0x00, &v, eth_DataType_uint8, 2000);
        printf("  当前 0x2100 = %u (ret=%d)   %s\n", (unsigned)v, rr,
               v == 0 ? "UART/串口 —— 主站拿不到 OP" :
               v == 1 ? "EtherCAT —— 正常" : "CANopen");
        if (v != 1) {
            huint8 one = 1;
            rr = eth_writeSDO(1, 0x2100, 0x00, &one, eth_DataType_uint8, 2000);
            printf("  写 0x2100 = 1 (EtherCAT)  ret=%d\n", rr);
            usleep(200000);
            eth_readSDO(1, 0x2100, 0x00, &v, eth_DataType_uint8, 2000);
            printf("  回读 0x2100 = %u  %s\n", (unsigned)v, v == 1 ? "✓ 切过来了" : "✗ 没切成功");
            printf("  提示：这只是临时切换；要掉电保持需要 0x2130 = 1 存进 EEPROM\n");
            printf("        重新跑一次不带 --force 的，看能不能进 OP\n");
        }
    }

    if (do_enable) {
        printf("\n============ 5. 试使能 ============\n");
        printf("先读几项和使能有关的配置：\n");
        sdo_u32(1, 0x253B, 0x00, "STO Mode (0=关 1=智能 2=报警 3=故障)");
        sdo_u32(1, 0x253C, 0x00, "STO Recover Mode (0=手动 1=自动)");
        sdo_u32(1, 0x2540, 0x00, "Drive Latched Warning");
        sdo_u32(1, 0x2541, 0x00, "Drive Latched Fault");
        sdo_u32(1, 0x2110, 0x00, "Brake Mode (0=手动 1=跟随使能)");
        sdo_u32(1, 0x2111, 0x00, "Manual Brake Output (0=松开 1=闭合)");
        sdo_u32(1, 0x6061, 0x00, "当前操作模式");
        sdo_u32(1, 0x6040, 0x00, "当前控制字");

        hint32 cur = 0;
        eth_getActualPosition(1, &cur);
        printf("\n当前实际位置 %d —— 把目标位置也设成它（CSP 下避免使能瞬间跳向旧目标）\n", cur);
        eth_setTargetPosition(1, cur);
        eth_setTargetVelocity(1, 0);
        eth_setTargetTorque(1, 0);

        huint16 sw = 0;
        eth_getStatusWord(1, &sw);
        printf("使能前状态字 0x%04X   状态 = 0x%02X\n", sw, sw & 0x6F);

        if ((sw & 0x08) != 0) {
            printf("\n!! 驱动器现在就在 Fault 态（bit3=1）—— 写控制字它不会理，使能必然失败。\n");
            printf("   先清故障：  %s %s --fault-reset\n", argv[0], ifname);
            printf("   或者在 GUI 上点「故障复位」。\n");
            eth_freeDLL();
            return 1;
        }

        brake_snapshot("使能前");

        printf("\n!! 3 秒后开始使能：关节会通电。机械上确认安全，手放在电源开关上 !!\n");
        sleep(3);

        const unsigned cw[]   = {0x0006, 0x0007, 0x000F};
        const char    *nm[]   = {"Shutdown (0x06)", "Switch On (0x07)", "Enable Op (0x0F)"};
        const unsigned want[] = {0x21, 0x23, 0x27};
        const char    *wnm[]  = {"Ready to switch on", "Switched on", "Operation enabled"};
        for (int i = 0; i < 3; ++i) {
            eth_setControlWord(1, cw[i]);
            int ok = 0;
            for (int k = 0; k < 12; ++k) {          // 最多等 600ms
                usleep(50000);
                eth_getStatusWord(1, &sw);
                if ((sw & 0x6F) == want[i]) { ok = 1; break; }
            }
            printf("  写 %-18s -> 状态字 0x%04X  状态 0x%02X  期望 0x%02X (%s) %s\n",
                   nm[i], sw, sw & 0x6F, want[i], wnm[i], ok ? "✓" : "✗ 卡住");
            if ((sw & 0x08) != 0) {
                huint16 e = 0;
                eth_getErrorCode(1, &e);
                printf("     bit3=1 进故障了，0x603F = 0x%04X %s\n", e, fault_text(e));
                sdo_u32(1, 0x2541, 0x00, "锁存故障 0x2541");
                break;
            }
        }

        if ((sw & 0x6F) == 0x27) {
            printf("\n  ✓ 已到 Operation enabled —— 趁没失能，立刻读一次抱闸：\n");
            brake_snapshot("使能后（尚未失能）");
            printf("\n  ★ 同时用身体判断（比读寄存器直接）：\n");
            printf("     · 用手轻转输出端：抱闸松了能转（有齿槽感/保持力矩），\n");
            printf("       没松就是硬锁死，一点都转不动\n");
            printf("     · 听声音：抱闸松开/吸合有很明显的咔哒声\n");
            printf("     · 看电源电流：抱闸线圈吸合会有一个电流台阶\n");
            printf("    15 秒后自动失能...\n");
            sleep(15);
        }

        eth_setControlWord(1, 0x0000);
        usleep(200000);
        eth_getStatusWord(1, &sw);
        printf("  收尾失能（写 0x0000）后状态字 0x%04X\n", sw);
    }

    eth_freeDLL();
    return 0;
}

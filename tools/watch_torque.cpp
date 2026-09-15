// 采样实际力矩/位置原始值，判断力矩读数抖动性质。
//
// 用法: sudo LD_LIBRARY_PATH=<SDK>/lib ./watch_torque <网卡名> [秒数] [从站] [--enable] [--rated N]
//   例: sudo LD_LIBRARY_PATH=.../lib ./watch_torque enx00e04c3a41c0 5 1 --enable --rated 84
//
// --enable: 先使能并"保持当前位置"（目标=当前实际位置，不会产生运动），再采样。
//           失能状态下力矩恒 0、位置被抱闸锁死，读数无参考价值，必须使能才可比对。
// --rated:  额定力矩 N·m，仅用于把 ‰ 换算成 N·m 便于阅读（不参与控制）。
#include "eu_ethercat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

static void cw(huint16 s, huint16 w, int ms)
{
    eth_setControlWord(s, w);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <ifname> [seconds] [slave] [--enable] [--rated N]\n", argv[0]);
        return 1;
    }
    const char* ifname = argv[1];
    const int seconds = (argc > 2 && argv[2][0] != '-') ? atoi(argv[2]) : 5;
    const huint16 slave = (argc > 3 && argv[3][0] != '-') ? (huint16)atoi(argv[3]) : 1;
    bool enable = false;
    double rated = 9.6;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--enable")) enable = true;
        if (!strcmp(argv[i], "--rated") && i + 1 < argc) rated = atof(argv[++i]);
    }

    int slaveCnt = 0;
    if (eth_initDLL(ifname, 2, &slaveCnt) != ETH_SUCCESS) { printf("init failed\n"); return 1; }
    printf("slave count: %d, 采样 slave %u 共 %d 秒（每 20ms 一次），额定按 %.1f N·m 换算\n\n",
           slaveCnt, (unsigned)slave, seconds, rated);

    if (enable) {
        hint32 pos = 0;
        if (eth_getActualPosition(slave, &pos) != ETH_SUCCESS) {
            printf("读位置失败，无法安全使能\n"); eth_freeDLL(); return 1;
        }
        printf("使能并保持当前位置 pos=%d（目标=当前实际位置，不产生运动）\n", pos);
        eth_setOperateMode(slave, eth_OperateMode_ProfilePosition);
        eth_setProfileVelocity(slave, 100000);
        eth_setProfileAcceleration(slave, 100000);
        eth_setProfileDeceleration(slave, 100000);
        eth_setTargetPosition(slave, pos);
        cw(slave, 0x06, 50);
        cw(slave, 0x07, 50);
        cw(slave, 0x0F, 50);
        cw(slave, 0x0F | 0x20, 20);
        cw(slave, 0x0F | 0x20 | 0x10, 300);
        huint16 sw = 0;
        eth_getStatusWord(slave, &sw);
        printf("状态字=0x%04X（0x0027/0x0637 等含 bit0-2 表示已使能）\n\n", sw);
    }

    std::vector<int> tor, pos;
    const int n = seconds * 50;
    for (int i = 0; i < n; ++i) {
        hint16 t = 0; hint32 p = 0;
        eth_getActualTorque(slave, &t);
        eth_getActualPosition(slave, &p);
        tor.push_back(t); pos.push_back(p);
        if (i % 10 == 0)
            printf("t=%4dms  tor=%6d‰ (%.3f N·m)  pos=%d\n",
                   i * 20, t, t * rated / 1000.0, p);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    int tlo = tor[0], thi = tor[0]; long tsum = 0;
    for (int x : tor) { tlo = std::min(tlo, x); thi = std::max(thi, x); tsum += x; }
    const double tmean = (double)tsum / tor.size();
    double tvar = 0; for (int x : tor) tvar += (x - tmean) * (x - tmean); tvar /= tor.size();
    int plo = pos[0], phi = pos[0];
    for (int x : pos) { plo = std::min(plo, x); phi = std::max(phi, x); }

    printf("\n力矩(‰): min=%d max=%d 幅度=%d 均值=%.1f 标准差=%.1f\n",
           tlo, thi, thi - tlo, tmean, std::sqrt(tvar));
    printf("       → 换算 = %.3f N·m 幅度（额定 %.1f N·m，即额定的 %.1f%%）\n",
           (thi - tlo) * rated / 1000.0, rated, (thi - tlo) / 10.0);
    printf("位置(脉冲): min=%d max=%d 幅度=%d\n", plo, phi, phi - plo);
    printf("       → 每输出圈 524288×减速比，%d 脉冲 ≈ %.6f 圈\n",
           phi - plo, (double)(phi - plo) / 52953088.0);

    if (enable) { eth_disable(slave); printf("\n已失能。\n"); }
    eth_freeDLL();
    return 0;
}

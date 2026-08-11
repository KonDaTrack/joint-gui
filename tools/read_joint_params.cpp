// 读取关节(驱动器)参数探针：遍历从站读取常用 CiA402 参数，用于标定/换关节时核对。
// 用法: sudo LD_LIBRARY_PATH=<SDK>/lib ./odtorque <网卡名>   例: ./odtorque enx00e04c3a41c0
#include "eu_ethercat.h"
#include <cstdio>

static void dump(huint16 s, huint16 idx, huint8 sub, const char* name, eth_DataType dt)
{
    huint32 v = 0;
    int r = eth_readSDO(s, idx, sub, &v, dt, 2000);
    printf("  0x%04X:%u %-24s = %u  (ret %d)\n", idx, sub, name, v, r);
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: %s <ifname>\n", argv[0]); return 1; }
    int slaveCnt = 0;
    if (eth_initDLL(argv[1], 2, &slaveCnt) != ETH_SUCCESS) { printf("init failed\n"); return 1; }
    printf("slave count: %d\n", slaveCnt);
    for (int s = 1; s <= slaveCnt; ++s) {
        printf("\nslave %d:\n", s);
        dump(s, 0x6080, 0, "max motor speed(rpm)", eth_DataType_uint32);
        dump(s, 0x6076, 0, "rated torque", eth_DataType_uint32);
        dump(s, 0x6075, 0, "rated current", eth_DataType_uint32);
        dump(s, 0x6072, 0, "max torque", eth_DataType_uint32);
        dump(s, 0x60E0, 0, "+torque limit", eth_DataType_uint32);
        dump(s, 0x60E1, 0, "-torque limit", eth_DataType_uint32);
        dump(s, 0x608F, 1, "encoder num", eth_DataType_uint32);
        dump(s, 0x608F, 2, "encoder den", eth_DataType_uint32);
        dump(s, 0x6091, 0, "gear ratio", eth_DataType_uint32);
        dump(s, 0x6081, 0, "profile velocity", eth_DataType_uint32);
        dump(s, 0x6083, 0, "profile acc", eth_DataType_uint32);
        dump(s, 0x6084, 0, "profile dec", eth_DataType_uint32);
        dump(s, 0x6099, 0, "homing speed", eth_DataType_uint32);
        dump(s, 0x6098, 0, "homing method", eth_DataType_uint32);
        dump(s, 0x60C2, 0, "max following error", eth_DataType_uint32);
        hint16 act = 0; eth_getActualTorque(s, &act);
        hint32 pos = 0; eth_getActualPosition(s, &pos);
        printf("  actual torque(permille)   = %d\n", act);
        printf("  actual pos(pulses)        = %d\n", pos);
    }
    eth_freeDLL();
    return 0;
}

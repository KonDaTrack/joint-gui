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

// 有符号量（软限位/偏置/目标位置，CiA402 为 int32）
static void dumps(huint16 s, huint16 idx, huint8 sub, const char* name)
{
    hint32 v = 0;
    int r = eth_readSDO(s, idx, sub, &v, eth_DataType_int32, 2000);
    printf("  0x%04X:%u %-24s = %d  (ret %d)\n", idx, sub, name, v, r);
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
        // 软限位（CiA402 0x607D）：判断关节是否限制行程，以及限位范围
        dumps(s, 0x607D, 1, "software MIN position");
        dumps(s, 0x607D, 2, "software MAX position");
        dumps(s, 0x607C, 0, "home offset");
        dumps(s, 0x607A, 0, "target position");
        // ---- 遥测快照（PDO 路径）。多从站时若两个从站的这些值完全相同，
        //      说明 SDK 的 PDO 遥测在多从站下串了（走 SDO 的对象索引不会串） ----
        hint32 tpos = 0, ttemp = 0;
        hint16 ttor = 0;
        huint16 tsw = 0, terr = 0;
        eth_OperateMode tmode = eth_OperateMode_Reserve;
        eth_getActualPosition(s, &tpos);
        eth_getActualTorque(s, &ttor);
        eth_getStatusWord(s, &tsw);
        eth_getErrorCode(s, &terr);
        eth_getOperateMode(s, &tmode);
        eth_getDriveTemper(s, &ttemp);
        printf("  --- 遥测(PDO) ---\n");
        printf("  状态字(0x6041)            = 0x%04X  (bit3=故障 bit0-2=状态)\n", tsw);
        printf("  故障码(0x603F)            = 0x%04X\n", terr);
        printf("  操作模式(0x6061)          = %d\n", (int)tmode);
        printf("  位置(0x6064)              = %d 脉冲\n", tpos);
        printf("  力矩(0x6077)              = %d ‰\n", ttor);
        printf("  驱动器温度                 = %d ℃\n", ttemp);
    }
    eth_freeDLL();
    return 0;
}

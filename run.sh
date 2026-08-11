#!/usr/bin/env bash
# 开发运行辅助：把仓库内 SDK 库目录聚合到 LD_LIBRARY_PATH 后启动程序。
# SDK 库自身带厂商构建机路径的 DT_RUNPATH，直接运行会找不到传递依赖
# （libsoem/libcyhcs_log 等），因此必须借助 LD_LIBRARY_PATH。
#
# 用法：
#   ./run.sh                      # 运行 ./build/joint_gui
#   ./run.sh ./build/joint_gui    # 显式指定可执行文件
set -euo pipefail
cd "$(dirname "$0")"

# 只匹配当前架构的 SDK 目录（x86_64 / aarch64），避免混入另一架构的库
ARCH="$(uname -m)"

BIN="${1:-./build/joint_gui}"

LIBS=""
for d in ../eyou_ethercat_phu_sdk_${ARCH}_linux_gnu_*/lib \
         ../eyou_canopen_sdk_PHU_${ARCH}_linux_gnu_*/lib; do
    [ -d "$d" ] && LIBS="${LIBS:+$LIBS:}$d"
done

if [ -z "$LIBS" ]; then
    echo "未找到 ${ARCH} 架构的 SDK 库目录" >&2
    exit 1
fi

exec env LD_LIBRARY_PATH="$LIBS" "$BIN" "${@:2}"

#!/usr/bin/env bash
# 关节模组监控台构建脚本
#   ./build.sh            # 本机构建（x86 或 ARM 板上）
#   ./build.sh arm        # x86 上交叉构建 aarch64
set -euo pipefail
cd "$(dirname "$0")"

MODE="${1:-native}"
BUILD_DIR="build"
EXTRA_ARGS=()

if [ "$MODE" = "arm" ]; then
    BUILD_DIR="build-arm"
    EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${PWD}/cmake/aarch64-linux-gnu.cmake")
    EXTRA_ARGS+=("-DJOINT_ARCH=aarch64")
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${EXTRA_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "== 可执行文件: $BUILD_DIR/joint_gui =="

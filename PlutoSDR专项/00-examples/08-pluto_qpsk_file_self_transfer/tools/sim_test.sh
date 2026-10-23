#!/usr/bin/env bash
#
# 仿真回归测试脚本（宿主机运行）。
# 工程根目录：本脚本所在目录的上一级（10-pluto_qpsk_file_self_transfer）。
# 用法：
#   bash tools/sim_test.sh                 # 默认参数
#   SIM_NOISE=0.1 bash tools/sim_test.sh   # 透传仿真环境变量
#   bash tools/sim_test.sh stress          # 额外跑一组压力测试
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM_DIR="$ROOT/doc/01-simulation"
SIM_BIN="$SIM_DIR/industrial_pipeline4"
SRC="$SIM_DIR/industrial_pipeline4.c"
IMG="$SIM_DIR/test.png"
OUT="$SIM_DIR/recovered.png"

cd "$SIM_DIR"

echo "==> 编译 $SRC"
gcc -Wall -O2 -o "$SIM_BIN" "$SRC" -lm -lc -lliquid

echo "==> 运行默认仿真"
"$SIM_BIN"

if cmp -s "$IMG" "$OUT"; then
    echo "==> PASS: recovered.png == test.png"
else
    echo "==> FAIL: recovered.png != test.png"
    exit 1
fi

if [ "${1:-}" = "stress" ]; then
    echo "==> 压力测试"
    # 随机相偏，重复 5 次
    for i in 1 2 3 4 5; do
        env -u SIM_PHASE "$SIM_BIN" >/dev/null
        cmp -s "$IMG" "$OUT" || { echo "==> FAIL: 随机相偏第 $i 次"; exit 1; }
    done
    # RX 早启动
    SIM_LEAD=100000 "$SIM_BIN" >/dev/null
    cmp -s "$IMG" "$OUT" || { echo "==> FAIL: SIM_LEAD=100000"; exit 1; }
    # 中途突发
    SIM_BURST=2000 SIM_BURST_POS=0.5 "$SIM_BIN" >/dev/null
    cmp -s "$IMG" "$OUT" || { echo "==> FAIL: SIM_BURST=2000"; exit 1; }
    echo "==> PASS: 压力测试全部通过"
fi

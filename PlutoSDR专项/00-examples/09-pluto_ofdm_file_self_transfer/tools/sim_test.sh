#!/usr/bin/env bash
#
# OFDM 仿真回归测试脚本（宿主机运行）。
# 工程根目录：本脚本所在目录的上一级（11-pluto_ofdm_file_self_transfer）。
# 用法：
#   bash tools/sim_test.sh                 # 默认参数
#   SIM_NOISE=0.2 bash tools/sim_test.sh   # 透传仿真环境变量
#   bash tools/sim_test.sh stress          # 额外跑一组压力测试
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM_DIR="$ROOT/doc/01-simulation"
SIM_BIN="$SIM_DIR/ofdm_pipeline"
SRC="$SIM_DIR/ofdm_pipeline.c"
IMG="$SIM_DIR/test.png"
OUT="$SIM_DIR/recovered.png"

cd "$SIM_DIR"

echo "==> 编译 $SRC"
gcc -Wall -O2 -o "$SIM_BIN" "$SRC" -lm -lc -lliquid

run_one() {
    local name="$1"
    shift
    rm -f "$OUT"
    echo "==> 运行: $name"
    "$SIM_BIN" "$@"
    if cmp -s "$IMG" "$OUT"; then
        echo "==> PASS: $name"
    else
        echo "==> FAIL: $name (recovered.png != test.png)"
        exit 1
    fi
}

run_one "默认参数"

if [ "${1:-}" = "stress" ]; then
    echo "==> 压力测试"
    # 随机相偏，重复 5 次
    for i in 1 2 3 4 5; do
        rm -f "$OUT"
        env -u SIM_PHASE "$SIM_BIN" >/dev/null
        cmp -s "$IMG" "$OUT" || { echo "==> FAIL: 随机相偏第 $i 次"; exit 1; }
    done
    # RX 早启动
    rm -f "$OUT"
    SIM_LEAD=100000 "$SIM_BIN" >/dev/null
    cmp -s "$IMG" "$OUT" || { echo "==> FAIL: SIM_LEAD=100000"; exit 1; }
    # 中途突发清零（重复分片被有意错开，短突发不应丢文件）
    rm -f "$OUT"
    SIM_BURST=5000 SIM_BURST_POS=0.5 "$SIM_BIN" >/dev/null
    cmp -s "$IMG" "$OUT" || { echo "==> FAIL: SIM_BURST=5000"; exit 1; }
    # 较强噪声 + 频偏
    rm -f "$OUT"
    SIM_NOISE=0.2 SIM_CFO=0.02 "$SIM_BIN" >/dev/null
    cmp -s "$IMG" "$OUT" || { echo "==> FAIL: SIM_NOISE=0.2 SIM_CFO=0.02"; exit 1; }
    echo "==> PASS: 压力测试全部通过"
fi

#!/usr/bin/env bash
# =============================================================================
#  build.sh — 命令行编译校验（需要先跑 setup_build_env.sh）
#
#  用法：
#      ./build.sh                          # 编译 ClockWeather
#      ./build.sh ClockWeatherSetup        # 编译指定工程
# =============================================================================
set -euo pipefail

WORK="$(cd "$(dirname "$0")" && pwd)"
BUILD="$WORK/.build"
ROOT="$(cd "$WORK/.." && pwd)"

SKETCH="${1:-ClockWeather}"
case "$SKETCH" in
    /*) ;;                       # 绝对路径原样用
    *)  SKETCH="$ROOT/$SKETCH" ;;  # 相对路径按工程根目录解析
esac

# 板级参数，对应 README 第三节 Arduino IDE 里的设置
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600"

[ -x "$BUILD/bin/arduino-cli" ] || { echo "请先运行 ./setup_build_env.sh"; exit 1; }
[ -d "$SKETCH" ] || { echo "找不到工程目录: $SKETCH"; exit 1; }

# ⚠ lv_conf.h 必须放在 libraries/ 下（和 lvgl 同级）LVGL 才能找到它，
#   所以每个工程各带一份，编译前同步过去 —— 否则编译的其实是上一个工程
#   留下的那份配置（比如少了 TEXTAREA / KEYBOARD 会直接编译失败）。
if [ -f "$SKETCH/lv_conf.h" ]; then
    cp "$SKETCH/lv_conf.h" "$BUILD/Arduino/libraries/lv_conf.h"
    echo "已同步 lv_conf.h <- $SKETCH/lv_conf.h"
fi

export HOME="$BUILD/home"
# ESP32 构建过程里的 PyInstaller 工具会解压到 TMPDIR，
# 如果 /tmp 挂载了 noexec（很多容器环境如此）就会启动失败，所以指到工作区。
export TMPDIR="$BUILD/tmp"
export PATH="$BUILD/py/python/bin:$PATH"

echo "编译 $SKETCH"
echo "FQBN = $FQBN"
echo

"$BUILD/bin/arduino-cli" --config-file "$BUILD/arduino-cli.yaml" compile \
    --fqbn "$FQBN" \
    --warnings more \
    "$SKETCH"

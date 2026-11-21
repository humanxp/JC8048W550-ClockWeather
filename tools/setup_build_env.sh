#!/usr/bin/env bash
# =============================================================================
#  setup_build_env.sh — 重建「命令行编译验证」环境（可选，日常用 Arduino IDE 不需要）
#
#  用途：不想开 Arduino IDE，想在命令行直接编译校验本工程时用。
#        全部装在 tools/.build/ 里，不污染系统目录，删掉即可。
#
#  用法：
#      cd tools
#      ./setup_build_env.sh          # 装环境（约 7~8GB，十几分钟）
#      ./build.sh                    # 编译校验
#      rm -rf .build                 # 不想要了就整个删掉
#
#  实测环境（本工程就是用它验证到 0 error / 0 warning 的）：
#      arduino-cli latest
#      esp32:esp32 3.3.11
#      lvgl 8.4.0
#      GFX Library for Arduino 1.6.8
#      Python 3.12.7（便携版，ESP32 构建脚本需要 python3）
# =============================================================================
set -euo pipefail

WORK="$(cd "$(dirname "$0")" && pwd)"
BUILD="$WORK/.build"
mkdir -p "$BUILD/bin" "$BUILD/tmp" "$BUILD/home"

# 关键：必须先屏蔽 HOME 并指定配置文件。
# 否则 arduino-cli 会去写 ~/.arduino15，在受限环境里直接报权限错误。
export HOME="$BUILD/home"
cat > "$BUILD/arduino-cli.yaml" <<EOF
directories:
  data: $BUILD/arduino15
  downloads: $BUILD/staging
  user: $BUILD/Arduino
logging:
  level: info
EOF

echo "==> 1/4 下载 arduino-cli"
cd "$BUILD/bin"
if [ ! -x ./arduino-cli ]; then
    curl -fsSL -o arduino-cli.tar.gz \
        "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz"
    tar xzf arduino-cli.tar.gz
fi
CLI="./arduino-cli --config-file $BUILD/arduino-cli.yaml"
$CLI version

echo "==> 2/4 配置就绪（数据目录全部指向 $BUILD，不碰 ~/.arduino15）"

echo "==> 3/4 安装 ESP32 核心 + 两个库"
# ⚠ 版本必须钉死：本工程就是在这一组版本上验证到 0 error / 0 warning 的。
#   esp32:esp32 3.3.11 + lvgl 8.4.0 + GFX Library for Arduino 1.6.8
$CLI core update-index
$CLI core install esp32:esp32@3.3.11
$CLI lib update-index
$CLI lib install "lvgl@8.4.0"
$CLI lib install "GFX Library for Arduino@1.6.8"

# lv_conf.h 必须放在 libraries/ 下，和 lvgl 文件夹同级。
# 注意：build.sh 编译前会用「被编译工程自己的 lv_conf.h」覆盖这一份，
#       所以这里放哪个工程的都行（放 ClockWeather 的当默认值）。
cp "$WORK/../ClockWeather/lv_conf.h" "$BUILD/Arduino/libraries/lv_conf.h"

# 给 Arduino_GFX 打补丁，放开 _panel_handle（RGB DMA 自愈要用）
node "$WORK/patch_gfx.js"

echo "==> 4/4 安装便携版 Python（ESP32 构建脚本要调用 python3）"
if [ ! -x "$BUILD/py/python/bin/python3" ]; then
    mkdir -p "$BUILD/py" && cd "$BUILD/py"
    curl -fsSL -o python.tar.gz \
      "https://github.com/astral-sh/python-build-standalone/releases/download/20241016/cpython-3.12.7%2B20241016-x86_64-unknown-linux-gnu-install_only.tar.gz"
    tar xzf python.tar.gz
fi
"$BUILD/py/python/bin/python3" --version

echo
echo "环境就绪。接下来执行 ./build.sh 编译校验。"

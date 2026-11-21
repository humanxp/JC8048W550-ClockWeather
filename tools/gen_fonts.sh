#!/usr/bin/env bash
# =============================================================================
#  gen_fonts.sh — 重新生成工程用的中文字库（ClockWeather / ClockWeatherSetup）
#
#  为什么要自己造字库？
#      LVGL 内置字体不含中文；常见做法是把整个思源黑体塞进 Flash（1MB+）。
#      这里按需裁剪：GB2312 一级字表 + 常见地名补充字，兼顾体积和覆盖率。
#
#  依赖：
#      node / npm
#      lv_font_conv 1.5.3   ->  npm install lv_font_conv@1.5.3
#      Noto Sans SC         ->  见下方 FONT_URL，下到 tools/fonts/ 目录
#
#  用法：
#      cd tools
#      npm install lv_font_conv@1.5.3
#      curl -L -o fonts/NotoSansSC-Regular.otf "$FONT_URL"
#      ./gen_fonts.sh ../ClockWeather
#      ./gen_fonts.sh ../ClockWeatherSetup
#      ./gen_fonts.sh ../ClockWeatherSetup font_cjk_22   # 只重生成指定的几个（快）
#
#  ⚠ lv_font_conv 1.5.x 生成的是 LVGL **v8** 格式，只能配 LVGL 8.x 使用。
# =============================================================================
set -euo pipefail

FONT_URL="https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
FONT_BOLD_URL="https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Bold.otf"

WORK="$(cd "$(dirname "$0")" && pwd)"
SKETCH_DIR="${1:-$(cd "$WORK/.." && pwd)/ClockWeather}"
# 第二个参数开始是「只生成这几个字体」，不传就是全生成
shift || true
ONLY=" $* "
LFC="$WORK/node_modules/.bin/lv_font_conv"
FONT="$WORK/fonts/NotoSansSC-Regular.otf"
FONT_BOLD="$WORK/fonts/NotoSansSC-Bold.otf"

size_of() {  # 兼容 Linux / macOS
    if stat -c%s "$1" >/dev/null 2>&1; then stat -c%s "$1"; else stat -f%z "$1"; fi
}

[ -x "$LFC" ]  || { echo "找不到 lv_font_conv，请先在 tools/ 下执行: npm install lv_font_conv@1.5.3"; exit 1; }
[ -f "$FONT" ] || { echo "找不到字体文件: $FONT"; echo "请先下载: curl -L -o '$FONT' '$FONT_URL'"; exit 1; }
[ -f "$FONT_BOLD" ] || { echo "找不到粗体: $FONT_BOLD"; echo "请先下载: curl -L -o '$FONT_BOLD' '$FONT_BOLD_URL'"; exit 1; }
[ -d "$SKETCH_DIR" ] || { echo "找不到工程目录: $SKETCH_DIR"; exit 1; }

echo ">> 生成字符集（GB2312 一级字表 + 常见地名补充字）"
node "$WORK/gen_charset.js" "$WORK/fonts/charset.txt"

echo ">> 提取天气描述用字（32px 字体只需要这一小撮）"
node "$WORK/extract_display.js" "$SKETCH_DIR" "$WORK/fonts/desc_charset.txt"

echo ">> 提取设置向导界面用字（28px 字体只需要这一小撮）"
node "$WORK/extract_ui_chars.js" "$SKETCH_DIR" "$WORK/fonts/setup_charset.txt"

# gen <字体变量名> <字号> <字符集文件> [字体文件]
gen() {  # 默认用常规体；传第 4 个参数可指定别的字重（时钟用粗体）
    # 指定了字体名过滤（脚本第二参数起）时，不在名单里的直接跳过
    if [ -n "${ONLY// /}" ] && [[ "$ONLY" != *" $1 "* ]]; then
        echo ">> 跳过 $1（不在本次指定的名单里）"
        return 0
    fi

    local src="${4:-$FONT}"
    echo ">> 生成 $1 (${2}px)$( [ "$src" = "$FONT_BOLD" ] && echo "  [粗体]" )"
    "$LFC" --font "$src" --size "$2" --bpp 4 --format lvgl \
           --lv-include lvgl.h --lv-font-name "$1" \
           --symbols "$(cat "$3")" -o "$SKETCH_DIR/$1.c"
    printf '   %-20s %8s 字节\n' "$1.c" "$(size_of "$SKETCH_DIR/$1.c")"
}

gen font_cjk_22    22  "$WORK/fonts/charset.txt"
gen font_cjk_32    32  "$WORK/fonts/desc_charset.txt"
gen font_clock_193 193 "$WORK/fonts/digits_clock.txt" "$FONT_BOLD"   # 看板时钟，粗体
gen font_digits_44  44 "$WORK/fonts/digits_ui.txt"
gen font_digits_32  32 "$WORK/fonts/digits_ui.txt"

# 下面两个只有设置向导版（ClockWeatherSetup）用得上：
#   font_cjk_28    向导里的列表 / 按钮 / 提示文字
#   font_clock_290 「平时显示」的满屏大时钟（2.685 × 290 ≈ 779px，一屏能放下的最大字号）
# 生成到 ClockWeather 里只会白白占 Flash（arduino-cli 会编译 sketch 目录下所有 .c），
# 所以这里按工程名判断一下。
SETUP_PROJECT=0
case "$SKETCH_DIR" in
    *ClockWeatherSetup*) SETUP_PROJECT=1 ;;
esac
if [ "$SETUP_PROJECT" = "1" ]; then
    gen font_cjk_28    28  "$WORK/fonts/setup_charset.txt"
    gen font_cjk_40    40  "$WORK/fonts/date_charset.txt"
    gen font_clock_290 290 "$WORK/fonts/digits_clock.txt" "$FONT_BOLD"
else
    echo ">> 跳过 font_cjk_28 / font_cjk_40 / font_clock_290（$SKETCH_DIR 用不到）"
fi

echo ">> 完成。"
echo "   想支持更多生僻字，把字追加进 $WORK/fonts/charset.txt 后重跑。"
echo "   注意：单个字库的位图总量不能超过 1MB（LV_FONT_FMT_TXT_LARGE=0 的限制）。"

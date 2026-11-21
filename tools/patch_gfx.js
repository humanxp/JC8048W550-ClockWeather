#!/usr/bin/env node
/**
 * =============================================================================
 *  patch_gfx.js — 给 Arduino_GFX 打一行补丁，放开 RGB panel 句柄
 *
 *  为什么需要：
 *      ClockWeather / ClockWeatherSetup 用 esp_lcd_rgb_panel_restart() 修
 *      ESP32-S3 RGB GDMA 的「永久失步」（随机花屏、只有重启才好）。
 *      这个函数要拿到 Arduino_ESP32RGBPanel 里的 _panel_handle，但它在
 *      private 区段里，不改库就编译不过：
 *          error: 'esp_lcd_panel_t* Arduino_ESP32RGBPanel::_panel_handle'
 *                 is private within this context
 *
 *  做法：
 *      找到 _panel_handle 声明，把它所在区段最近的那个 `private:` 改成
 *      `public:`。只动这一处，库的其它部分不受影响。
 *
 *  用法：
 *      node patch_gfx.js <Arduino_ESP32RGBPanel.h 路径>
 *      node patch_gfx.js            # 自动在 tools/.build 里找
 * =============================================================================
 */
'use strict';

const fs = require('fs');
const path = require('path');

function findHeader() {
    if (process.argv[2]) return process.argv[2];

    const home = process.env.USERPROFILE || process.env.HOME || '';
    const roots = [
        /* 本仓库自带的命令行编译环境 */
        path.join(__dirname, '.build', 'Arduino', 'libraries'),
        path.join(__dirname, '.build', 'home', 'Arduino', 'libraries'),
        /* Arduino IDE 的库目录（Windows 常见位置 + macOS / Linux） */
        path.join(home, 'Documents', 'Arduino', 'libraries'),
        path.join(home, 'Arduino', 'libraries'),
        path.join(home, 'OneDrive', 'Documents', 'Arduino', 'libraries'),
        'C:\\Program Files (x86)\\Arduino\\libraries',
    ];

    for (const root of roots) {
        if (!root || !fs.existsSync(root)) continue;
        for (const name of fs.readdirSync(root)) {
            if (!/^GFX/i.test(name)) continue;
            const p = path.join(root, name, 'src', 'databus', 'Arduino_ESP32RGBPanel.h');
            if (fs.existsSync(p)) return p;
        }
    }
    return null;
}

const file = findHeader();
if (!file) {
    console.error('找不到 Arduino_ESP32RGBPanel.h，请把路径作为参数传进来');
    process.exit(1);
}

let src = fs.readFileSync(file, 'utf8');
const i = src.indexOf('_panel_handle');
if (i < 0) {
    console.error(`在 ${file} 里找不到 _panel_handle`);
    process.exit(1);
}

const at = src.lastIndexOf('private:', i);
if (at < 0) {
    console.log('已经是 public，无需改动：', file);
    process.exit(0);
}

src = src.slice(0, at) + 'public:' + src.slice(at + 'private:'.length);
fs.writeFileSync(file, src, 'utf8');

/* 打印上下文，方便核对改对了地方 */
const lines = src.split('\n');
const ln = src.slice(0, at).split('\n').length;
console.log(`已打补丁（第 ${ln} 行 private: -> public:）：${file}`);
for (let k = Math.max(0, ln - 1); k < Math.min(lines.length, ln + 4); k++) {
    console.log(`  ${k + 1}: ${lines[k]}`);
}

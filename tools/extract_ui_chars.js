#!/usr/bin/env node
/**
 * =============================================================================
 *  extract_ui_chars.js — 从设置向导的源码里提取「界面上真正会显示的字」
 *
 *  为什么只扫字符串字面量：
 *      源码注释里全是中文（"这个是修 XXX 的关键"），把注释也算进去的话
 *      字库会白白胖胖一大圈。界面文字只会出现在 "..." 里，扫这个就够准。
 *
 *  用法：
 *      node extract_ui_chars.js <工程目录> <输出文件>
 * =============================================================================
 */
'use strict';

const fs = require('fs');
const path = require('path');

const sketch = process.argv[2];
const out    = process.argv[3] || path.join(__dirname, 'fonts', 'setup_charset.txt');

/* 28px 字库只服务于设置向导的「固定文案」，扫这几个文件就够了。
 *
 * ⚠ 不要往里加 region_data.cpp：
 *   省份 / 城市名是「数据」不是文案，一共 437 个字；塞进 28px 字库要多花
 *   一百多 KB Flash，而且「新疆维吾尔自治区」这种 8 字省名在 28px 下也放不进
 *   250px 宽的省份列。
 *   约定：界面上凡是显示「用户数据」的地方（地区名、WiFi SSID）一律用
 *   22px 的全字库 font_cjk_22（3900+ 字，含全部地名），那条路保证不缺字；
 *   28px 只用于按钮 / 标题 / 提示这些写死在源码里的文案。
 */
const FILES = [
    'setup_ui.cpp',
    'net_time.cpp',   /* 连接失败的原因文字会显示在向导的报错面板上 */
    'ui.cpp'          /* 顶栏「设置」按钮 */
];

/* 界面上会用到、但不一定出现在字面量里的符号 */
const ALWAYS = '←→…·°×≥≤，。、；：！？（）《》“”‘’—￥%';

const set = new Set();

for (const f of FILES) {
    const p = path.join(sketch, f);
    if (!fs.existsSync(p)) continue;

    const src = fs.readFileSync(p, 'utf8');
    /* 匹配双引号字符串，跳过转义 */
    for (const m of src.matchAll(/"((?:[^"\\]|\\.)*)"/g)) {
        for (const ch of m[1]) {
            const cp = ch.codePointAt(0);
            if (cp >= 0x4E00 && cp <= 0x9FFF) set.add(ch);   /* 汉字 */
        }
    }
}

for (const ch of ALWAYS) set.add(ch);
for (let c = 0x20; c <= 0x7E; c++) set.add(String.fromCharCode(c));

const text = [...set].join('');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, text, 'utf8');

const cjk = [...set].filter(c => c.codePointAt(0) >= 0x4E00).join('');
console.log('   向导界面用字 %d 个（其中汉字 %d 个）: %s', set.size, cjk.length, cjk);

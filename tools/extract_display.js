/**
 * 从 weather.cpp 的 weather_text() 里提取所有会显示在屏幕上的汉字。
 * 32px 字体只服务于「天气描述」这一个标签，所以字集可以做得非常小。
 */
const fs = require('fs');
const path = require('path');

const sketch = process.argv[2];
const out    = process.argv[3] || '/tmp/desc_charset.txt';

const src = fs.readFileSync(path.join(sketch, 'weather.cpp'), 'utf8');
const start = src.indexOf('const char *weather_text');
const end   = src.indexOf('WeatherIconType weather_icon_type', start);
if (start < 0 || end < 0) {
  console.error('在 weather.cpp 里定位 weather_text() 失败');
  process.exit(1);
}

const set = new Set();
for (const m of src.slice(start, end).matchAll(/"([^"\\]*)"/g)) {
  for (const ch of m[1]) {
    const cp = ch.codePointAt(0);
    if (cp >= 0x4E00 && cp <= 0x9FFF) set.add(ch);
  }
}
for (const ch of '加载中°') set.add(ch);   // 界面上的「加载中」占位符

fs.writeFileSync(out, [...set].join(''), 'utf8');
console.log('   天气描述用字 %d 个: %s', set.size, [...set].join(''));

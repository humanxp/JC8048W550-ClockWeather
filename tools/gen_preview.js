#!/usr/bin/env node
/**
 * =============================================================================
 *  gen_preview.js — 画 ClockWeatherSetup 的效果示意图（SVG）
 *
 *  画 5 张 800x480 的屏，并排放在一张 SVG 里：
 *      ① 大时钟（平时显示）      ② 时间 + 天气看板（点一下屏幕出现）
 *      ③ 连接 WiFi（扫描列表）   ④ 输入密码（软键盘）   ⑤ 选择地区
 *
 *  坐标是按 ui.cpp / setup_ui.cpp 里的布局常量画的。改了那边的布局，
 *  这个脚本也要跟着改，否则图和实物会对不上。
 *
 *  用法：
 *      node gen_preview.js ../ClockWeatherSetup/preview.svg
 * =============================================================================
 */
'use strict';

const fs = require('fs');

const W = 800, H = 480, GAP = 44, PAD = 40, CAP = 56;
const X = i => PAD + i * (W + GAP);
const OUT = [];

/* 面板：一个屏幕的外框 + 标题 */
const panel = (i, title) => {
  OUT.push(`  <g transform="translate(${X(i)},${CAP})">`);
  OUT.push(`    <rect width="${W}" height="${H}" rx="10" fill="url(#bg)" stroke="#2E5A85"/>`);
  OUT.push(`    <text x="${W / 2}" y="-18" font-size="20" fill="#C3D9F0" text-anchor="middle">${title}</text>`);
  OUT.push(`    <clipPath id="clip${i}"><rect width="${W}" height="${H}" rx="10"/></clipPath>`);
  OUT.push(`    <g clip-path="url(#clip${i})">`);
};
const end = () => OUT.push('    </g>', '  </g>');

/* 胶囊按钮 */
const btn = (x, y, w, h, text, fg, bg, stroke, fs) =>
  OUT.push(`    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="12" fill="${bg}" stroke="${stroke || '#2E5A85'}"/>`,
           `    <text x="${x + w / 2}" y="${y + h / 2 + (fs || 28) * 0.34}" font-size="${fs || 28}" fill="${fg}" text-anchor="middle">${text}</text>`);

/* 卡片 */
const card = (x, y, w, h, r) =>
  OUT.push(`    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r || 20}" fill="url(#card)" stroke="#2E5A85"/>`);

/* 信号 4 格 */
const bars = (x, y, level) => {
  const h = [5, 8, 11, 14];
  for (let i = 0; i < 4; i++)
    OUT.push(`    <rect x="${x + i * 6}" y="${y + 14 - h[i]}" width="4" height="${h[i]}" rx="1" fill="${i < level ? '#FFFFFF' : '#2A4C73'}"/>`);
};

const text = (x, y, s, size, fill, anchor, extra) =>
  OUT.push(`    <text x="${x}" y="${y}" font-size="${size}" fill="${fill}"${anchor ? ` text-anchor="${anchor}"` : ''}${extra || ''}>${s}</text>`);

/* 简单天气图标（太阳 + 云） */
const wxIcon = (cx, cy, s) => {
  OUT.push(`    <circle cx="${cx}" cy="${cy - s * 0.12}" r="${s * 0.26}" fill="#FFD60A" opacity="0.9"/>`,
           `    <ellipse cx="${cx - s * 0.1}" cy="${cy + s * 0.22}" rx="${s * 0.34}" ry="${s * 0.2}" fill="#E8F2FF"/>`,
           `    <ellipse cx="${cx + s * 0.16}" cy="${cy + s * 0.2}" rx="${s * 0.26}" ry="${s * 0.16}" fill="#C3D9F0"/>`);
};

/* 软键盘 */
const keyboard = (KX, KY, KW, KH) => {
  const kp = 6, kg = 6;
  const rows = [
    ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
    ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'],
    ['⇧', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '⌫'],
    ['1#', ',', '空格', '.', '✓'],
  ];
  const rh = (KH - 2 * kp - 3 * kg) / 4;
  rows.forEach((keys, ri) => {
    const y = KY + kp + ri * (rh + kg);
    const total = KW - 2 * kp, n = keys.length;
    const kw = (total - (n - 1) * kg) / n;
    keys.forEach((k, i) => {
      const x = KX + kp + i * (kw + kg);
      OUT.push(`    <rect x="${x.toFixed(1)}" y="${y.toFixed(1)}" width="${kw.toFixed(1)}" height="${rh.toFixed(1)}" rx="8" fill="#1C3E63" stroke="#2E5A85"/>`,
               `    <text x="${(x + kw / 2).toFixed(1)}" y="${(y + rh / 2 + 8).toFixed(1)}" font-size="22" fill="#FFFFFF" text-anchor="middle">${k}</text>`);
    });
  });
};

/* ================= ① 大时钟（平时显示）+ 底部当前天气 ================= */
panel(0, '① 大时钟（平时显示 · 最下面是当前详细天气 · 点屏幕切看板）');

/* 右上角：联网状态（大时钟页不放按钮，保持干净） */
OUT.push(`    <circle cx="167" cy="34" r="5" fill="#30D158"/>`);
text(780, 41, '已联网 · 更新 14:22', 22, '#30D158', 'end');

text(400, 222, '08:42', 290, '#FFFFFF', 'middle');
text(400, 332, '9月20日 周六', 40, '#C3D9F0', 'middle');

/* 底部天气条 */
card(24, 352, 752, 116, 20);
wxIcon(46, 410, 56);
text(80, 396, '23.4°', 44, '#FFFFFF');
text(80, 444, '多云', 22, '#C3D9F0');
OUT.push(`    <line x1="276" y1="372" x2="276" y2="448" stroke="#2E5A85" opacity="0.7"/>`);
const idleRows = [['体感', '25.1°C'], ['湿度', '62%'], ['风速', '3.2m/s'], ['气压', '1013hPa']];
idleRows.forEach((r, i) => {
  const x = 292 + i * 121;
  text(x, 398, r[0], 22, '#8FB2D4');
  text(x, 446, r[1], 22, '#FFFFFF');
});
end();

/* ================= ② 时间 + 天气看板 ================= */
panel(1, '② 看板（点屏幕出现 · 5 分钟没操作自动回大时钟 · 三天预报下方是降水概率条）');
text(26, 38, '宁波市 9月20日 周六', 22, '#FFFFFF');
btn(532, 9, 96, 40, '刷新天气', '#C3D9F0', '#1C3E63', '#2E5A85', 22);
btn(636, 9, 76, 40, '大时钟', '#C3D9F0', '#1C3E63', '#2E5A85', 22);
btn(720, 9, 64, 40, '设置', '#C3D9F0', '#1C3E63', '#2E5A85', 22);
OUT.push(`    <circle cx="259" cy="24" r="5" fill="#30D158"/>`);
text(524, 38, '已联网 · 更新 14:22', 22, '#30D158', 'end');
OUT.push(`    <line x1="0" y1="56" x2="800" y2="56" stroke="#2E5A85" opacity="0.5"/>`);

/* 时钟 + 秒进度条 */
text(271, 232, '08:42', 193, '#FFFFFF', 'middle');
OUT.push(`    <rect x="80" y="262" width="300" height="10" rx="5" fill="#2A4C73"/>`,
         `    <rect x="80" y="262" width="185" height="10" rx="5" fill="#0A84FF"/>`);
text(404, 290, '37', 44, '#0A84FF');

/* 天气卡 */
card(558, 66, 226, 280, 22);
wxIcon(620, 105, 76);
text(600, 140, '23.4°', 44, '#FFFFFF');
text(574, 180, '多云', 32, '#C3D9F0');
OUT.push(`    <line x1="574" y1="216" x2="768" y2="216" stroke="#2E5A85" opacity="0.7"/>`);
const rows = [['体感', '25.1°C'], ['湿度', '62%'], ['风速', '3.2m/s'], ['气压', '1013hPa']];
rows.forEach((r, i) => {
  const y = 240 + i * 30;
  text(574, y, r[0], 22, '#8FB2D4');
  text(768, y, r[1], 22, '#FFFFFF', 'end');
  if (i < 3) OUT.push(`    <line x1="574" y1="${y + 9}" x2="768" y2="${y + 9}" stroke="#2E5A85" opacity="0.7"/>`);
});

/* 三天预报（每张卡下面那条是降水概率：长度 = 概率，颜色分三档） */
const days = [['今天', '26°', '18°', 0], ['明天', '24°', '17°', 55], ['后天', '28°', '19°', 85]];
const popColor = p => (p < 25 ? '#8FB2D4' : (p <= 60 ? '#64D2FF' : '#0A84FF'));
const fcBarW = 148;                       /* ui.cpp 的 FC_BAR_W */
days.forEach((d, i) => {
  const x = 16 + i * 260, pop = d[3];
  card(x, 356, 248, 112, 20);
  text(x + 16, 386, d[0], 22, '#FFFFFF');
  wxIcon(x + 210, 380, 52);
  text(x + 16, 428, d[1], 32, '#FFFFFF');
  text(x + 66, 428, '/', 32, '#8FB2D4');
  text(x + 80, 428, d[2], 32, '#8FB2D4');
  OUT.push(`    <rect x="${x + 16}" y="448" width="${fcBarW}" height="6" rx="3" fill="#2A4C73"/>`);
  const w = pop > 0 ? Math.max(6, Math.round(pop / 100 * fcBarW)) : 0;
  if (w > 0) OUT.push(`    <rect x="${x + 16}" y="448" width="${w}" height="6" rx="3" fill="${popColor(pop)}"/>`);
  text(x + 232, 458, `${pop}%`, 22, popColor(pop), 'end');
});
end();

/* ================= ③ 连接 WiFi ================= */
panel(2, '③ 连接 WiFi（异步扫描 · 按信号排序 · 同名只留最强）');
btn(24, 14, 100, 44, '返回', '#C3D9F0', '#1C3E63');
text(148, 46, '连接 WiFi', 28, '#FFFFFF');
btn(532, 14, 88, 44, '地区', '#C3D9F0', '#1C3E63');
btn(628, 14, 148, 44, '重新扫描', '#64D2FF', '#1C3E63');
text(24, 102, '找到 6 个网络，点击即可连接', 22, '#8FB2D4');

const aps = [
  ['MyHome-2.4G', 4, true, true],
  ['ChinaNet-8xKz', 3, true, false],
  ['TP-LINK_5F2C', 2, true, false],
  ['Guest-WiFi', 4, false, false],
  ['Xiaomi_AX3000', 2, true, false],
];
const LX = 24, LY = 112, LW = 752, RH = 62, RG = 8;
aps.forEach((a, i) => {
  const y = LY + i * (RH + RG);
  const [name, lv, sec, saved] = a;
  OUT.push(`    <rect x="${LX}" y="${y}" width="${LW}" height="${RH}" rx="12" fill="url(#card)" stroke="${saved ? '#30D158' : '#2E5A85'}"/>`);
  bars(LX + 18, y + 24, lv);
  if (sec) text(LX + 52, y + 39, '加密', 20, '#8FB2D4');
  text(LX + 108, y + 39, name, 28, '#FFFFFF');
  if (saved) text(LX + LW - 20, y + 39, '已保存', 20, '#30D158', 'end');
  else text(LX + LW - 20, y + 39, '>', 28, '#8FB2D4', 'end');
});
{
  const y = LY + aps.length * (RH + RG);
  OUT.push(`    <rect x="${LX}" y="${y}" width="${LW}" height="${RH}" rx="12" fill="none" stroke="#2E5A85"/>`);
  text(LX + 20, y + 39, '+', 28, '#64D2FF');
  text(LX + 58, y + 39, '手动输入网络名（隐藏的 WiFi）', 22, '#C3D9F0');
}
end();

/* ================= ④ 输入密码 ================= */
panel(3, '④ 输入密码（LVGL 软键盘 · 可显示 / 隐藏 · 失败能重试）');
btn(24, 14, 100, 44, '返回', '#C3D9F0', '#1C3E63');
text(148, 46, '输入密码', 28, '#FFFFFF');
btn(676, 14, 100, 44, '连接', '#FFFFFF', '#0A84FF', '#64D2FF');
text(24, 114, '网络', 22, '#8FB2D4');
text(110, 114, 'MyHome-2.4G', 28, '#FFFFFF');
text(24, 176, '密码', 22, '#8FB2D4');
OUT.push(`    <rect x="110" y="148" width="460" height="50" rx="10" fill="#143050" stroke="#0A84FF"/>`);
text(128, 181, '••••••••', 28, '#FFFFFF');
btn(584, 148, 92, 50, '显示', '#C3D9F0', '#1C3E63');
text(24, 228, '密码区分大小写', 22, '#8FB2D4');
keyboard(20, 240, 760, 228);
end();

/* ================= ⑤ 选择地区 ================= */
panel(4, '⑤ 选择地区（左省右市 · 坐标跟着走）');
btn(24, 14, 100, 44, '返回', '#C3D9F0', '#1C3E63');
text(148, 46, '选择地区', 28, '#FFFFFF');
btn(676, 14, 100, 44, '完成', '#FFFFFF', '#0A84FF', '#64D2FF');
text(24, 102, '已选：浙江省 · 宁波市', 22, '#8FB2D4');
{
  const PY = 116, PH = 340, RH2 = 46, RG2 = 6;
  const provs = ['北京市', '天津市', '河北省', '山西省', '内蒙古自治区', '辽宁省', '吉林省', '黑龙江省', '上海市', '江苏省', '浙江省', '安徽省', '福建省'];
  provs.forEach((p, i) => {
    const y = PY + i * (RH2 + RG2);
    const sel = p === '浙江省';
    OUT.push(`    <rect x="24" y="${y}" width="250" height="${RH2}" rx="12" fill="${sel ? '#0A84FF' : '#1C3E63'}" stroke="${sel ? '#64D2FF' : '#2E5A85'}"/>`,
             `    <text x="38" y="${y + 31}" font-size="26" fill="${sel ? '#FFFFFF' : '#C3D9F0'}">${p}</text>`);
  });
  const cities = ['杭州市', '宁波市', '温州市', '嘉兴市', '湖州市', '绍兴市', '金华市', '衢州市'];
  cities.forEach((c, i) => {
    const y = PY + i * (RH2 + RG2);
    const sel = c === '宁波市';
    OUT.push(`    <rect x="290" y="${y}" width="486" height="${RH2}" rx="12" fill="${sel ? '#0A84FF' : '#1C3E63'}" stroke="${sel ? '#64D2FF' : '#2E5A85'}"/>`,
             `    <text x="304" y="${y + 31}" font-size="26" fill="${sel ? '#FFFFFF' : '#C3D9F0'}">${c}</text>`);
  });
}
end();

/* ================= 组装 ================= */
const totalW = PAD * 2 + 5 * W + 4 * GAP;
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${totalW}" height="${CAP + H + 40}" viewBox="0 0 ${totalW} ${CAP + H + 40}"
     font-family="'SF Pro Display',-apple-system,'PingFang SC','Noto Sans SC','Microsoft YaHei',sans-serif">
  <!--
    ClockWeatherSetup 效果示意图（800x480 五屏并排）
    坐标是按 ui.cpp / setup_ui.cpp 里的布局常量画的，改了代码记得重跑：
        node gen_preview.js ../ClockWeatherSetup/preview.svg
  -->
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#061020"/><stop offset="1" stop-color="#123A66"/>
    </linearGradient>
    <linearGradient id="card" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#1C3E63"/><stop offset="1" stop-color="#143050"/>
    </linearGradient>
  </defs>
  <rect width="100%" height="100%" fill="#04080F"/>
${OUT.join('\n')}
</svg>
`;

const out = process.argv[2] || '../ClockWeatherSetup/preview.svg';
fs.writeFileSync(out, svg, 'utf8');
console.log('written', out, svg.length, 'bytes');

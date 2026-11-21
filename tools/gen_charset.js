const dec = new TextDecoder('gb18030');
const fs = require('fs');
const path = require('path');
// 输出路径：命令行第一个参数，默认 fonts/charset.txt
const OUT = process.argv[2] || path.join(__dirname, 'fonts', 'charset.txt');
// GB2312 一级字表 (区16-55)
const l1 = new Set();
for (let hi=0xB0; hi<=0xD7; hi++) for (let lo=0xA1; lo<=0xFE; lo++){
  const s = dec.decode(Buffer.from([hi,lo]));
  if (s.length===1 && s!=='\uFFFD' && s.codePointAt(0)>0x2000) l1.add(s);
}
// 中国地级及以上城市 / 常见地名用字（二级字表里才有的字，靠这份名单补齐）
const places = `
北京 上海 天津 重庆 石家庄 唐山 秦皇岛 邯郸 邢台 保定 张家口 承德 沧州 廊坊 衡水
太原 大同 阳泉 长治 晋城 朔州 晋中 运城 忻州 临汾 吕梁
呼和浩特 包头 乌海 赤峰 通辽 鄂尔多斯 呼伦贝尔 巴彦淖尔 乌兰察布 兴安 锡林郭勒 阿拉善
沈阳 大连 鞍山 抚顺 本溪 丹东 锦州 营口 阜新 辽阳 盘锦 铁岭 朝阳 葫芦岛
长春 吉林 四平 辽源 通化 白山 松原 白城 延边 延吉
哈尔滨 齐齐哈尔 鸡西 鹤岗 双鸭山 大庆 伊春 佳木斯 七台河 牡丹江 黑河 绥化 大兴安岭
南京 无锡 徐州 常州 苏州 南通 连云港 淮安 盐城 扬州 镇江 泰州 宿迁
杭州 宁波 温州 嘉兴 湖州 绍兴 金华 衢州 舟山 台州 丽水
合肥 芜湖 蚌埠 淮南 马鞍山 淮北 铜陵 安庆 黄山 滁州 阜阳 宿州 六安 亳州 池州 宣城
福州 厦门 莆田 三明 泉州 漳州 南平 龙岩 宁德
南昌 景德镇 萍乡 九江 新余 鹰潭 赣州 吉安 宜春 抚州 上饶
济南 青岛 淄博 枣庄 东营 烟台 潍坊 济宁 泰安 威海 日照 临沂 德州 聊城 滨州 菏泽
郑州 开封 洛阳 平顶山 安阳 鹤壁 新乡 焦作 濮阳 许昌 漯河 三门峡 南阳 商丘 信阳 周口 驻马店 济源
武汉 黄石 十堰 宜昌 襄阳 鄂州 荆门 孝感 荆州 黄冈 咸宁 随州 恩施 仙桃 潜江 天门 神农架
长沙 株洲 湘潭 衡阳 邵阳 岳阳 常德 张家界 益阳 郴州 永州 怀化 娄底 湘西
广州 韶关 深圳 珠海 汕头 佛山 江门 湛江 茂名 肇庆 惠州 梅州 汕尾 河源 阳江 清远 东莞 中山 潮州 揭阳 云浮
南宁 柳州 桂林 梧州 北海 防城港 钦州 贵港 玉林 百色 贺州 河池 来宾 崇左
海口 三亚 三沙 儋州 五指山 琼海 文昌 万宁 东方 定安 屯昌 澄迈 临高 白沙 昌江 乐东 陵水 保亭 琼中
成都 自贡 攀枝花 泸州 德阳 绵阳 广元 遂宁 内江 乐山 南充 眉山 宜宾 广安 达州 雅安 巴中 资阳 阿坝 甘孜 凉山
贵阳 六盘水 遵义 安顺 毕节 铜仁 黔西南 黔东南 黔南
昆明 曲靖 玉溪 保山 昭通 丽江 普洱 临沧 楚雄 红河 文山 西双版纳 大理 德宏 怒江 迪庆
拉萨 日喀则 昌都 林芝 山南 那曲 阿里
西安 铜川 宝鸡 咸阳 渭南 延安 汉中 榆林 安康 商洛
兰州 嘉峪关 金昌 白银 天水 武威 张掖 平凉 酒泉 庆阳 定西 陇南 临夏 甘南
西宁 海东 海北 黄南 海南 果洛 玉树 海西
银川 石嘴山 吴忠 固原 中卫
乌鲁木齐 克拉玛依 吐鲁番 哈密 昌吉 博尔塔拉 巴音郭楞 阿克苏 克孜勒苏 喀什 和田 伊犁 塔城 阿勒泰 石河子 阿拉尔 图木舒克 五家渠 北屯 铁门关 双河 可克达拉 昆玉 胡杨河
香港 澳门 台北 新北 桃园 台中 台南 高雄 基隆 新竹 嘉义 彰化 南投 云林 屏东 宜兰 花莲 台东 澎湖 金门 马祖
义乌 昆山 常熟 张家港 江阴 宜兴 溧阳 太仓 慈溪 余姚 诸暨 温岭 瑞安 乐清 海宁 桐乡 平湖 嵊州 兰溪 东阳 永康
增城 从化 番禺 花都 宝安 龙岗 顺德 南海 三水 高明 新会 台山 开平 鹤山 恩平
浏阳 宁乡 醴陵 湘乡 韶山 耒阳 常宁 武冈 汨罗 临湘 津市 沅江 资兴
新郑 荥阳 新密 登封 巩义 偃师 孟州 沁阳 卫辉 辉县 长垣 林州 禹州 长葛 义马 灵宝 邓州 永城 项城 汝州 济源
滕州 龙口 莱阳 莱州 招远 蓬莱 栖霞 海阳 青州 诸城 寿光 安丘 高密 昌邑 曲阜 邹城 新泰 肥城 荣成 乳山 乐陵 禹城 临清 邹平
`;
const chars = new Set();
for (const ch of places.replace(/\s+/g,'')) chars.add(ch);
const extra = [];
for (const c of chars) if (!l1.has(c)) extra.push(c);
console.log('地名中不属于一级字表的字 (%d):', extra.length, extra.join(''));

// 追加「额外字表」：由 gen_regions.js 从全国省市名里汇总出来，
// 保证界面上能选到的地名都有字形（缺字会显示成方框）。
const extraFile = path.join(path.dirname(OUT), 'charset_extra.txt');
let regionExtra = [];
if (fs.existsSync(extraFile)) {
  const text = fs.readFileSync(extraFile, 'utf8');
  for (const c of text) if (c.codePointAt(0) > 0x2E7F) chars.add(c);
  regionExtra = [...text];
  console.log('地区名补充字 (%d):', regionExtra.length);
}

// 组装最终字符集
let out = [];
for (let c=0x20; c<=0x7E; c++) out.push(String.fromCharCode(c));
out.push(...'°·×÷→←↑↓±≈℃′″，。、；：！？（）《》“”‘’—…￥%');
out = out.concat([...l1], extra, regionExtra);
const uniq = Array.from(new Set(out.join(''))).join('');
console.log('最终字符集大小:', uniq.length);
fs.writeFileSync(OUT, uniq, 'utf8');

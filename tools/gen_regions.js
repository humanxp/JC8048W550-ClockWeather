#!/usr/bin/env node
/**
 * =============================================================================
 *  gen_regions.js — 生成 ClockWeatherSetup/region_data.cpp（全国省市 + 坐标）
 *
 *  数据来源：阿里 DataV.GeoAtlas 行政区划接口
 *      https://geo.datav.aliyun.com/areas_v3/bound/100000_full.json
 *      https://geo.datav.aliyun.com/areas_v3/bound/<省adcode>_full.json
 *  取每个 feature 的 properties.center，也就是官方给的行政中心点。
 *
 *  坐标系说明：
 *      DataV 的 center 是 GCJ-02（火星坐标，高德那套），
 *      Open-Meteo 用 WGS84，两者在国内相差约 50~600 米。
 *      天气预报是按几公里到十几公里的网格算的，这点偏差完全不影响结果，
 *      所以这里不做坐标转换（转换需要非线性算法，不值当）。
 *
 *  用法：
 *      cd tools && node gen_regions.js
 *  数据会缓存到 tools/.build/regions/，断网也能重跑。
 * =============================================================================
 */
'use strict';

const fs = require('fs');
const path = require('path');

const WORK = __dirname;
const CACHE = path.join(WORK, '.build', 'regions');
const SKETCH = path.join(WORK, '..', 'ClockWeatherSetup');
const OUT_CPP = path.join(SKETCH, 'region_data.cpp');
const OUT_EXTRA = path.join(WORK, 'fonts', 'charset_extra.txt');

const BASE = 'https://geo.datav.aliyun.com/areas_v3/bound/';

/* 直辖市：市级就是它自己，直接列成一项 */
const MUNICIPALITIES = new Set([110000, 120000, 310000, 500000]);
/* 港澳：DataV 下面只有「区」，对选天气没意义，整块作为一项 */
const SAR = new Set([810000, 820000]);

/* 台湾省：DataV 没有 710000_full.json（404），这里手工补主要城市。
   坐标取自公开的城市中心点（WGS84），用来查天气足够。 */
const FALLBACK_CITIES = {
    710000: [
        ['台北市', 25.0330, 121.5654],
        ['新北市', 25.0114, 121.4618],
        ['桃园市', 24.9936, 121.3010],
        ['台中市', 24.1477, 120.6736],
        ['台南市', 22.9998, 120.2269],
        ['高雄市', 22.6273, 120.3014],
        ['基隆市', 25.1276, 121.7392],
        ['新竹市', 24.8138, 120.9675],
        ['苗栗市', 24.5602, 120.8214],
        ['彰化市', 24.0756, 120.5440],
        ['南投市', 23.9099, 120.6855],
        ['云林县', 23.7092, 120.4313],
        ['嘉义市', 23.4801, 120.4491],
        ['屏东县', 22.6761, 120.4942],
        ['宜兰县', 24.7021, 121.7377],
        ['花莲县', 23.9910, 121.6112],
        ['台东县', 22.7583, 121.1444],
        ['澎湖县', 23.5711, 119.5793],
        ['金门县', 24.4365, 118.3187],
        ['连江县', 26.1605, 119.9516],
    ],
};

function cachePath(adcode) {
    return path.join(CACHE, `${adcode}_full.json`);
}

async function fetchJson(adcode) {
    const file = cachePath(adcode);
    if (fs.existsSync(file) && fs.statSync(file).size > 0) {
        return JSON.parse(fs.readFileSync(file, 'utf8'));
    }
    const url = BASE + (adcode === 100000 ? '100000_full.json' : `${adcode}_full.json`);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status} ${url}`);
    const text = await res.text();
    fs.mkdirSync(CACHE, { recursive: true });
    fs.writeFileSync(file, text);
    return JSON.parse(text);
}

function cstr(s) {
    /* 生成 C 字符串字面量：转义反斜杠和双引号；中文保持 UTF-8 原样 */
    return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
}

/* 行政中心点经纬度：优先 center，其次 centroid */
function centerOf(props) {
    const c = Array.isArray(props.center) && props.center.length === 2 ? props.center
            : Array.isArray(props.centroid) && props.centroid.length === 2 ? props.centroid
            : null;
    if (!c) return null;
    const lng = Number(c[0]);
    const lat = Number(c[1]);
    if (!isFinite(lng) || !isFinite(lat)) return null;
    return { lat, lng };
}

async function build() {
    console.log('>> 拉取省级列表 100000_full.json');
    const country = await fetchJson(100000);
    const provinces = country.features
        .map(f => f.properties)
        .filter(p => p && p.level === 'province' && typeof p.adcode === 'number')
        .sort((a, b) => a.adcode - b.adcode);

    console.log(`   共 ${provinces.length} 个省级行政区`);

    const result = [];

    for (const p of provinces) {
        const name = p.name;
        const adcode = p.adcode;
        const pc = centerOf(p);
        let cities = [];

        const flat = MUNICIPALITIES.has(adcode) || SAR.has(adcode);

        if (!flat) {
            try {
                const sub = await fetchJson(adcode);
                const feats = sub.features.map(f => f.properties)
                    .filter(x => x && typeof x.name === 'string');
                /* 地级行政区：level === 'city'；个别省份（如台湾）用别的 level */
                let picked = feats.filter(x => x.level === 'city');
                if (picked.length === 0) picked = feats.filter(x => x.level === 'district');
                if (picked.length === 0) picked = feats;

                for (const c of picked) {
                    const cc = centerOf(c);
                    if (!cc) continue;
                    /* 「省直辖县级行政区划」这种不是真实地名，跳过 */
                    if (/行政区划|直辖/.test(c.name)) continue;
                    cities.push({ name: c.name, lat: cc.lat, lon: cc.lng });
                }
            } catch (e) {
                console.log(`   ! ${name} (${adcode}) 子级拉取失败：${e.message}`);
            }
        }

        if (cities.length === 0 && FALLBACK_CITIES[adcode]) {
            cities = FALLBACK_CITIES[adcode].map(([n, la, lo]) => ({ name: n, lat: la, lon: lo }));
        }
        if (cities.length === 0 && pc) {
            cities = [{ name, lat: pc.lat, lon: pc.lng }];
        }
        if (cities.length === 0) {
            console.log(`   ! ${name} 没有坐标，跳过`);
            continue;
        }
        result.push({ name, cities });
        console.log(`   ${name.padEnd(6, '　')} ${String(cities.length).padStart(3)} 项`);
    }

    return result;
}

function emit(regions) {
    const total = regions.reduce((n, r) => n + r.cities.length, 0);

    let out = '';
    out += '/**\n';
    out += ' * ============================================================================\n';
    out += ' *  region_data.cpp  —  全国省 / 市 + 坐标（自动生成，不要手改）\n';
    out += ' *\n';
    out += ' *  生成脚本：tools/gen_regions.js\n';
    out += ' *  数据来源：阿里 DataV.GeoAtlas 行政区划（properties.center 行政中心点）\n';
    out += ' *  坐标系　：GCJ-02（高德火星坐标）。与天气接口用的 WGS84 相差 50~600 米，\n';
    out += ' *            对按公里级网格计算的天气来说可以忽略，故不做转换。\n';
    out += ' *\n';
    out += ` *  省级 ${regions.length} 项，市级 ${total} 项。\n`;
    out += ' * ============================================================================\n';
    out += ' */\n\n';
    out += '#include "region_data.h"\n\n';

    regions.forEach((r, i) => {
        out += `/* ---- ${i + 1}. ${r.name} ---- */\n`;
        out += `static const RegionCity k_cities_${i}[] = {\n`;
        for (const c of r.cities) {
            out += `    { ${cstr(c.name)}, ${c.lat.toFixed(4)}f, ${c.lon.toFixed(4)}f },\n`;
        }
        out += '};\n\n';
    });

    out += 'const RegionProvince g_region_provinces[] = {\n';
    regions.forEach((r, i) => {
        out += `    { ${cstr(r.name)}, k_cities_${i}, ${r.cities.length} },\n`;
    });
    out += '};\n\n';
    out += 'const uint16_t g_region_province_count =\n';
    out += '    (uint16_t)(sizeof(g_region_provinces) / sizeof(g_region_provinces[0]));\n\n';
    out += `/* ------------------------------------------------------------------ */
/*  按名字查找（用于把 Flash 里存的地区名还原成列表里的选中项）        */
/* ------------------------------------------------------------------ */
static bool name_eq(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr) return false;
    return strcmp(a, b) == 0;
}

int region_find_province(const char *name)
{
    if (name == nullptr) return -1;
    for (uint16_t i = 0; i < g_region_province_count; i++) {
        if (name_eq(g_region_provinces[i].name, name)) return (int)i;
    }
    return -1;
}

int region_find_city(int province_index, const char *name)
{
    if (name == nullptr) return -1;
    if (province_index < 0 || province_index >= (int)g_region_province_count) return -1;

    const RegionProvince *p = &g_region_provinces[province_index];
    for (uint16_t i = 0; i < p->city_count; i++) {
        if (name_eq(p->cities[i].name, name)) return (int)i;
    }
    return -1;
}
`;

    fs.writeFileSync(OUT_CPP, out, 'utf8');
    console.log(`>> 写入 ${path.relative(process.cwd(), OUT_CPP)}  (${regions.length} 省 / ${total} 市)`);

    /* 把地区名里出现的汉字汇总出来，供字库生成脚本并集，避免缺字 */
    const chars = new Set();
    for (const r of regions) {
        for (const ch of r.name) if (ch.codePointAt(0) > 0x2E7F) chars.add(ch);
        for (const c of r.cities) for (const ch of c.name) if (ch.codePointAt(0) > 0x2E7F) chars.add(ch);
    }
    fs.writeFileSync(OUT_EXTRA, Array.from(chars).join(''), 'utf8');
    console.log(`>> 写入 ${path.relative(process.cwd(), OUT_EXTRA)}  (${chars.size} 个汉字)`);
}

build().then(emit).catch(e => {
    console.error('生成失败：', e);
    process.exit(1);
});

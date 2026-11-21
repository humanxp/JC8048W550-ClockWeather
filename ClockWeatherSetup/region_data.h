/**
 * ============================================================================
 *  region_data.h  —  全国省 / 市 + 坐标（数据表接口）
 *
 *  数据本体在 region_data.cpp 里，由脚本自动生成，不要手改：
 *      cd tools && node gen_regions.js
 *
 *  数据来源：阿里 DataV.GeoAtlas 行政区划接口的 properties.center
 *            （行政中心点，GCJ-02，对公里级网格的天气来说精度足够）。
 *
 *  用法：
 *      for (i in provinces)            省份名 g_region_provinces[i].name
 *          for (j in .cities)          城市名 .cities[j].name + 经纬度
 * ============================================================================
 */
#pragma once

#include <stdint.h>
#include <string.h>

/** 一个城市（或直辖市 / 港澳）及其行政中心坐标 */
struct RegionCity {
    const char *name;
    float       lat;
    float       lon;
};

/** 一个省 / 自治区 / 直辖市，以及它下面的城市列表 */
struct RegionProvince {
    const char         *name;
    const RegionCity   *cities;
    uint16_t            city_count;
};

/** 全部省份（按行政区划代码升序） */
extern const RegionProvince g_region_provinces[];
extern const uint16_t       g_region_province_count;

/** 按名字找省份下标，找不到返回 -1。 */
int region_find_province(const char *name);

/** 在指定省份里按名字找城市下标，找不到返回 -1。 */
int region_find_city(int province_index, const char *name);

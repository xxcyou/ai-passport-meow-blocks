// main/app/catcube_levels.h —— 「猫猫方块」关卡表接口(生成物的契约)。
//
// 数据由 tools/gen_catcube_levels.py 生成到 catcube_levels_data.c,不要手改。
// 每个关卡:一个 w*h 的色块网格 + 一组标准答案格;色块数恒等于猫咪数 k,
// 因此"每块最多一只猫"等价于"每块恰好一只猫",配合唯一解构成完整谜题。
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t w;             /* 宽(列数) */
    uint8_t h;             /* 高(行数) */
    uint8_t k;             /* 目标猫咪数 = 色块数 */
    const uint8_t *cells;  /* w*h 个色块 id(0..k-1),行优先,index = r*w+c */
    const uint8_t *answer; /* k 个答案格 index,行优先 */
} catcube_level_def_t;

extern const catcube_level_def_t CATCUBE_LEVELS[];
extern const int CATCUBE_LEVEL_COUNT;

// 81..400 关:同样由生成器离线产出,运行期只查表。
extern const catcube_level_def_t CATCUBE_EXTRA_LEVELS[];
extern const int CATCUBE_EXTRA_COUNT;

// 关卡提供者(catcube_levels.c):1 起,永不返回 NULL。400 关之后是按关号做的
// 确定性对称/换色变换 —— 同一关号永远同一张图。
const catcube_level_def_t *catcube_level_at(int level_index);

// 烘焙表的关数(80 + 320)。超过它的关号走变换。
int catcube_level_table_count(void);

#ifdef __cplusplus
}
#endif

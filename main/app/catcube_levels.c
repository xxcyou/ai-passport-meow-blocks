// main/app/catcube_levels.c —— 关卡提供者:查表 + 无尽模式的确定性变换。
//
// 80 关之后的关卡策略:
//   * 81..400 关是离线用 tools/gen_catcube_extra.c 生成的成品表(唯一解已逐关验证);
//   * 400 关之后没有上限,但设备端现场生成一关要几十秒,不可接受。所以对更远的关卡
//     做**确定性变换**:取基础表里的某一关,按关号选一个正方形的对称变换(转置/镜像/
//     旋转)再整体换色。这些变换都是双射,行/列/色块/邻接关系原样保留,唯一解也保留。
//
// 关键性质:同一关号永远得到同一张图 —— 变换只依赖关号,不依赖时间、随机数或芯片状态。
// 所以"重置游戏后再玩第 100 关还是这一关"成立。
#include "app/catcube_levels.h"

#include "app/catcube_logic.h"

#include <string.h>

#define LEVEL_TRANSFORM_BASE (CATCUBE_LEVEL_COUNT + CATCUBE_EXTRA_COUNT)

static uint8_t s_cells[CATCUBE_MAX_CELLS];
static uint8_t s_answer[CATCUBE_MAX_SIDE];
static catcube_level_def_t s_view;

int catcube_level_table_count(void) { return LEVEL_TRANSFORM_BASE; }

// 8 个正方形的对称变换(0=原样,1=转置,2=左右镜像,3=上下镜像,4..6=旋转,7=转置+镜像)。
static void map_xy(int n, int sym, int r, int c, int *orr, int *occ) {
    switch (sym & 7) {
        case 1:
            *orr = c;
            *occ = r;
            break; // 转置
        case 2:
            *orr = r;
            *occ = n - 1 - c;
            break; // 左右镜像
        case 3:
            *orr = n - 1 - r;
            *occ = c;
            break; // 上下镜像
        case 4:
            *orr = c;
            *occ = n - 1 - r;
            break; // 顺时针 90
        case 5:
            *orr = n - 1 - r;
            *occ = n - 1 - c;
            break; // 180
        case 6:
            *orr = n - 1 - c;
            *occ = r;
            break; // 顺时针 270
        case 7:
            *orr = n - 1 - c;
            *occ = n - 1 - r;
            break; // 转置 + 镜像
        default:
            *orr = r;
            *occ = c;
            break;
    }
}

static const catcube_level_def_t *transform(const catcube_level_def_t *base, uint32_t variant) {
    const int n = base->w;
    if (n <= 0 || n > CATCUBE_MAX_SIDE) return base;

    const int sym = (int)(variant % 8);
    const uint32_t shift = variant / 8;
    const uint8_t k = base->k;

    for (int r = 0; r < base->h; r++) {
        for (int c = 0; c < base->w; c++) {
            int nr = r;
            int nc = c;
            map_xy(n, sym, r, c, &nr, &nc);
            uint8_t region = base->cells[r * base->w + c];
            // 换色:整体平移色块编号(双射,不改变"每块一只猫")
            region = (uint8_t)((region + shift) % k);
            s_cells[nr * n + nc] = region;
        }
    }
    for (int i = 0; i < k; i++) {
        const int idx = base->answer[i];
        int nr = idx / base->w;
        int nc = idx % base->w;
        map_xy(n, sym, idx / base->w, idx % base->w, &nr, &nc);
        s_answer[i] = (uint8_t)(nr * n + nc);
    }

    s_view.w = base->w;
    s_view.h = base->h;
    s_view.k = k;
    s_view.cells = s_cells;
    s_view.answer = s_answer;
    return &s_view;
}

const catcube_level_def_t *catcube_level_at(int level_index) {
    if (level_index < 1) return NULL;
    if (level_index <= CATCUBE_LEVEL_COUNT) return &CATCUBE_LEVELS[level_index - 1];
    if (level_index <= LEVEL_TRANSFORM_BASE) {
        return &CATCUBE_EXTRA_LEVELS[level_index - CATCUBE_LEVEL_COUNT - 1];
    }
    // 400 关之后:确定性变换。基础表用整张表(80 + 320)循环取,保证花样足够多。
    const uint32_t beyond = (uint32_t)(level_index - LEVEL_TRANSFORM_BASE - 1);
    const int total = LEVEL_TRANSFORM_BASE;
    const int base_index = (int)(beyond % (uint32_t)total) + 1;
    const catcube_level_def_t *base = (base_index <= CATCUBE_LEVEL_COUNT)
                                          ? &CATCUBE_LEVELS[base_index - 1]
                                          : &CATCUBE_EXTRA_LEVELS[base_index - CATCUBE_LEVEL_COUNT - 1];
    return transform(base, beyond / (uint32_t)total);
}

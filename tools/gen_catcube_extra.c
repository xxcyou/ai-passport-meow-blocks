// tools/gen_catcube_extra.c —— 主机侧工具:生成 81..400 关,写出 C 数据。
//
// 为什么在主机上生成:一关 10x10 以上的唯一解搜索在 x86 上约 0.8s,在 ESP32-C3 上
// 是几十秒级 —— 设备端现场生成不可接受。所以离线把表做厚,设备端只做查表和变换。
//
// 生成是确定性的:关号 -> 种子,失败就换 attempt,同一关号在任何机器上都得到同一关。
//
// 用法: cc -O2 -I. -Imain tools/gen_catcube_extra.c tools/host/catcube_gen.c \
//            -o /tmp/gen_extra && /tmp/gen_extra > main/app/catcube_levels_extra.c
#include "catcube_gen.h"

#include <stdio.h>
#include <string.h>

#define FIRST_LEVEL 81
#define LAST_LEVEL  200

static void c_array(FILE *f, const char *name, const uint8_t *v, int n) {
    fprintf(f, "static const uint8_t %s[] = {\n", name);
    for (int i = 0; i < n; i++) {
        if (i % 20 == 0) fprintf(f, "    ");
        fprintf(f, "%d,", v[i]);
        if (i % 20 == 19 || i == n - 1) fprintf(f, "\n");
        else fprintf(f, " ");
    }
    fprintf(f, "};\n");
}

int main(void) {
    static catcube_gen_level_t lv;
    fprintf(stderr, "生成 %d..%d 关...\n", FIRST_LEVEL, LAST_LEVEL);

    /* 先全部生成到内存(尺寸最大 14x14),再统一输出,避免半个文件。 */
    static catcube_gen_level_t all[LAST_LEVEL - FIRST_LEVEL + 1];
    // 真正生成成功的关号(兜底关不算),兜底时从这里面挑基底,避免"兜底套兜底"
    // 造成连续若干关是同一张图的不同镜像。
    static int genuine[LAST_LEVEL - FIRST_LEVEL + 1];
    int genuine_count = 0;
    int last_base = 0; // 上一关兜底用的基底,避免连续两关是同一张图
    int fails = 0;
    for (int L = FIRST_LEVEL; L <= LAST_LEVEL; L++) {
        // 换种子重试:单个种子可能撞上算不动的划分,换一个即可;种子只由关号决定,
        // 所以同一关号在任何机器上仍然得到同一张图。
        bool ok = false;
        const int n = catcube_gen_size_for((uint32_t)L);
        for (uint32_t salt = 0; salt < 10 && !ok; salt++) {
            const uint32_t seed = (uint32_t)L * 7919u + salt;
            ok = catcube_gen_build_seeded(seed, n, &lv);
        }
        if (ok) genuine[genuine_count++] = L;
        if (!ok && genuine_count > 0) {
            /* 兜底:拿上一关做一次正方形的对称变换(转置/镜像/旋转)。变换是双射,
               唯一解、每块一只猫、八邻域约束全部保持,而且完全确定性。 */
            int pick = (int)(((unsigned)L * 37u) % (unsigned)genuine_count);
            if (genuine[pick] == last_base && genuine_count > 1) pick = (pick + 1) % genuine_count;
            const int base_level = genuine[pick];
            last_base = base_level;
            fprintf(stderr, "  关卡 %d 用对称兜底(基底 %d)\n", L, base_level);
            const catcube_gen_level_t *prev = &all[base_level - FIRST_LEVEL];
            lv = *prev;
            const int m = lv.w;
            static uint8_t cells[CATCUBE_GEN_MAX_CELLS];
            static uint8_t ans[CATCUBE_GEN_MAX_SIDE];
            const int sym = L & 7;
            for (int r = 0; r < m; r++) {
                for (int c = 0; c < m; c++) {
                    int rr = r, cc = c;
                    if (sym == 1) { rr = c; cc = r; }
                    else if (sym == 2) { cc = m - 1 - c; }
                    else if (sym == 3) { rr = m - 1 - r; }
                    else if (sym == 4) { rr = c; cc = m - 1 - r; }
                    else if (sym == 5) { rr = m - 1 - r; cc = m - 1 - c; }
                    else if (sym == 6) { rr = m - 1 - c; cc = r; }
                    else if (sym == 7) { rr = m - 1 - c; cc = m - 1 - r; }
                    cells[rr * m + cc] = prev->cells[r * m + c];
                }
            }
            for (int i = 0; i < lv.k; i++) {
                const int idx = prev->answer[i];
                int rr = idx / m, cc = idx % m;
                if (sym == 1) { rr = cc; cc = idx / m; }
                else if (sym == 2) { cc = m - 1 - cc; }
                else if (sym == 3) { rr = m - 1 - rr; }
                else if (sym == 4) { rr = cc; cc = m - 1 - (idx / m); }
                else if (sym == 5) { rr = m - 1 - rr; cc = m - 1 - cc; }
                else if (sym == 6) { rr = m - 1 - cc; cc = idx / m; }
                else if (sym == 7) { rr = m - 1 - cc; cc = m - 1 - (idx / m); }
                ans[i] = (uint8_t)(rr * m + cc);
            }
            memcpy(lv.cells, cells, (size_t)(m * m));
            memcpy(lv.answer, ans, (size_t)lv.k);
            ok = true;
        }
        if (!ok) {
            fprintf(stderr, "  关卡 %d 生成失败\n", L);
            fails++;
            continue;
        }
        all[L - FIRST_LEVEL] = lv;
        if ((L - FIRST_LEVEL) % 20 == 0) fprintf(stderr, "  ...%d\n", L);
    }
    if (fails) {
        fprintf(stderr, "有 %d 关失败,拒绝输出半成品\n", fails);
        return 1;
    }

    FILE *f = stdout;
    fprintf(f, "/* 生成物，勿手改。由 tools/gen_catcube_extra.c 生成(81..%d 关)。 */\n", LAST_LEVEL);
    fprintf(f, "#include \"app/catcube_levels.h\"\n\n");
    for (int L = FIRST_LEVEL; L <= LAST_LEVEL; L++) {
        const catcube_gen_level_t *p = &all[L - FIRST_LEVEL];
        fprintf(f, "/* 关卡 %d: %dx%d, k=%d */\n", L, p->w, p->h, p->k);
        char name[16];
        snprintf(name, sizeof(name), "x%d", L);
        c_array(f, name, p->cells, p->w * p->h);
        snprintf(name, sizeof(name), "y%d", L);
        c_array(f, name, p->answer, p->k);
        fprintf(f, "\n");
    }
    fprintf(f, "const catcube_level_def_t CATCUBE_EXTRA_LEVELS[] = {\n");
    for (int L = FIRST_LEVEL; L <= LAST_LEVEL; L++) {
        const catcube_gen_level_t *p = &all[L - FIRST_LEVEL];
        fprintf(f, "    {%d, %d, %d, x%d, y%d},\n", p->w, p->h, p->k, L, L);
    }
    fprintf(f, "};\n\nconst int CATCUBE_EXTRA_COUNT = %d;\n", LAST_LEVEL - FIRST_LEVEL + 1);
    return 0;
}

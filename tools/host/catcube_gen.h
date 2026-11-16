// main/app/catcube_gen.h —— 无尽模式的关卡生成器(纯 C,可在设备上跑,也可主机测试)。
//
// 需求:80 关之后没有上限,关卡"全靠算法",但必须**确定性** —— 同一关号永远生成
// 同一张图(重置游戏、换机器、重刷固件都一样)。所以这里的随机数种子只由关号
// 决定,生成过程中不读时间、不读芯片 ID、不做任何非确定的事。
//
// 算法(与 tools/gen_catcube_levels.py 同源):
//   1. 先放一组满足"行/列不重复 + 八邻域不接触"的猫坐标(标准答案);
//   2. 以这些猫为种子随机生长出 k 个四连通色块,每块恰好含一只猫并覆盖全盘;
//   3. 用行推进的位掩码 DFS 全盘枚举解:只有恰好 1 个解才算成品;
//   4. 不是唯一解就"迁移杀解":取一个多余解,把它里面某个非答案格移进"已含该解
//      另一格"的相邻色块,该解立刻失效;每步校验源色块连通且仍含自己的答案。
//
// 所有枚举都有节点预算,超预算就换下一个确定性种子(attempt+1),不会卡死。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/catcube_levels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CATCUBE_GEN_MAX_SIDE  14
#define CATCUBE_GEN_MAX_CELLS (CATCUBE_GEN_MAX_SIDE * CATCUBE_GEN_MAX_SIDE)

// 生成结果自带的存储(避免设备端动态分配)。
typedef struct {
    uint8_t w;
    uint8_t h;
    uint8_t k;
    uint8_t cells[CATCUBE_GEN_MAX_CELLS];
    uint8_t answer[CATCUBE_GEN_MAX_SIDE];
} catcube_gen_level_t;

// 关号(1 起) -> 棋盘边长。80 关之后按 10 关一个阶梯在 8..14 之间循环,14 是硬上限。
int catcube_gen_size_for(uint32_t level_index);

// 生成一关。成功返回 true 并填 out(可直接当 catcube_level_def_t 的底层存储)。
bool catcube_gen_build(uint32_t level_index, catcube_gen_level_t *out);

// 指定种子和边长的底层入口:离线制表时用它做"换种子重试",仍然完全确定性。
bool catcube_gen_build_seeded(uint32_t seed, int n, catcube_gen_level_t *out);

// 枚举解的数量(最多数到 cap)。node_budget 用尽时返回已数到的数量并把
// *budget_hit 置真。给测试与自检用。
int catcube_gen_count_solutions(const catcube_level_def_t *lv, int cap, uint32_t node_budget,
                                uint32_t *nodes_used, bool *budget_hit);

#ifdef __cplusplus
}
#endif

#!/usr/bin/env python3
"""放猫咪（Cat Cube）解谜关卡生成器 —— 逆向生成法。

规则（每关同时生效，全部由本脚本自证）:
  1. 每个四连通同色色块内最多 1 只猫（色块数 == 猫咪数 k，故每块恰好 1 只）。
  2. 任意一行、任意一列最多 1 只猫。
  3. 任意两只猫八邻域互不接触。
  4. 每关有且仅有唯一解。

逆向生成三步:
  第一步：在空白棋盘上放 k 个满足规则 2/3 的答案坐标（行互异、列互异、八邻域两两不接触）。
  第二步：把棋盘划成 k 个四连通色块，每块恰好含 1 个答案格，覆盖全盘。
  第三步：全盘回溯枚举所有满足规则 1/2/3 的 k 猫解，解数必须恰好为 1。

唯一解靠"迁移修复"达成（拒绝采样在大盘上命中率趋零）:
  枚举到非答案解 S 后，挑 S 中一个非答案格 x，把它从原色块 A 迁移到邻居色块 B，
  且 B 中已含 S 的另一个格子 —— 于是 S 立刻失效（B 同时含两只 S 猫），
  同时校验 A 去掉 x 后仍四连通且仍含自己的答案格。反复迭代直到解数 == 1。

用法:
  python3 tools/gen_catcube_levels.py            生成 80 关并写出全部产物
  python3 tools/gen_catcube_levels.py --check    只读取已有产物做独立校验（不重写）
  python3 tools/gen_catcube_levels.py --report   打印已有产物的逐关报告
  python3 tools/gen_catcube_levels.py --probe    可行性/耗时探测（诊断用，不写产物）
"""

import argparse
import json
import math
import os
import random
import sys
import time
from collections import deque

# --------------------------------------------------------------------------
# 关卡阶梯表（由 lead 穷举证明后修订：2x2/3x3 数学上不可能唯一，最小 4x4）
# L1-L8 用 k=n-1（猫少、色块大），L9 起 k=n。
# --------------------------------------------------------------------------
LEVEL_TABLE = [
    # L1-L3 4x4 k3
    (4, 4, 3), (4, 4, 3), (4, 4, 3),
    # L4-L6 4x4 k4
    (4, 4, 4), (4, 4, 4), (4, 4, 4),
    # L7-L8 5x5 k4
    (5, 5, 4), (5, 5, 4),
    # L9-L12 5x5 k5
    (5, 5, 5), (5, 5, 5), (5, 5, 5), (5, 5, 5),
    # L13-L17 6x6 k6
    (6, 6, 6), (6, 6, 6), (6, 6, 6), (6, 6, 6), (6, 6, 6),
    # L18-L20 7x7 k7
    (7, 7, 7), (7, 7, 7), (7, 7, 7),
    # L21-L30 8x8/9x9 k8/k9 交替
    (8, 8, 8), (9, 9, 9), (8, 8, 8), (9, 9, 9), (8, 8, 8),
    (9, 9, 9), (8, 8, 8), (9, 9, 9), (8, 8, 8), (9, 9, 9),
    # L31-L40 9x9/10x10 k9/k10
    (9, 9, 9), (10, 10, 10), (9, 9, 9), (10, 10, 10), (9, 9, 9),
    (10, 10, 10), (9, 9, 9), (10, 10, 10), (9, 9, 9), (10, 10, 10),
    # L41-L50 10x10/11x11 k10/k11
    (10, 10, 10), (11, 11, 11), (10, 10, 10), (11, 11, 11), (10, 10, 10),
    (11, 11, 11), (10, 10, 10), (11, 11, 11), (10, 10, 10), (11, 11, 11),
    # L51-L60 11x11/12x12 k11/k12
    (11, 11, 11), (12, 12, 12), (11, 11, 11), (12, 12, 12), (11, 11, 11),
    (12, 12, 12), (11, 11, 11), (12, 12, 12), (11, 11, 11), (12, 12, 12),
    # L61-L70 12x12/13x13 k12/k13
    (12, 12, 12), (13, 13, 13), (12, 12, 12), (13, 13, 13), (12, 12, 12),
    (13, 13, 13), (12, 12, 12), (13, 13, 13), (12, 12, 12), (13, 13, 13),
    # L71-L80 13x13/14x14 k13/k14
    (13, 13, 13), (14, 14, 14), (13, 13, 13), (14, 14, 14), (13, 13, 13),
    (14, 14, 14), (13, 13, 13), (14, 14, 14), (13, 13, 13), (14, 14, 14),
]
LEVEL_COUNT = 80
assert len(LEVEL_TABLE) == LEVEL_COUNT, 'level table must contain 80 entries'

def effective_table():
    """生效阶梯表：L1-L20 保持原表；L21-L80 每个阶梯内按尺寸升序重排。

    任务允许同一阶梯内调整尺寸排列顺序（阶梯内至少两种尺寸、尺寸在 8..14、k=n），
    这样"难度升序"与"尺寸升序"一致，阶梯内难度天然更容易单调。
    """
    tbl = list(LEVEL_TABLE)
    for t in range(2, TIER_COUNT):
        lo, hi = t * TIER_SIZE, (t + 1) * TIER_SIZE
        block = sorted(tbl[lo:hi], key=lambda x: (x[0], x[2]))
        tbl[lo:hi] = block
    return tbl


TIER_SIZE = 10
TIER_COUNT = LEVEL_COUNT // TIER_SIZE

# 生效阶梯表（L21-L80 每阶梯内按尺寸升序重排，见 effective_table()）
EFFECTIVE_TABLE = effective_table()

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(REPO_ROOT, 'main', 'app', 'catcube_levels.h')
OUT_C = os.path.join(REPO_ROOT, 'main', 'app', 'catcube_levels_data.c')
OUT_JSON = os.path.join(REPO_ROOT, 'tools', 'catcube_levels.json')
OUT_TXT = os.path.join(REPO_ROOT, 'docs', 'reference', 'catcube', 'catcube-levels.txt')

NEIGH4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
NEIGH8 = ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1))

MAX_ATTEMPTS = 6           # 单次尝试最多换多少个随机种子（外层还有候选池与补生成）
MAX_MIGRATIONS = 1200      # 单次尝试内最多多少次迁移
COUNT_NODE_BUDGET = 3_000_000


def inb(r, c, w, h):
    return 0 <= r < h and 0 <= c < w


# --------------------------------------------------------------------------
# 规则判定基础
# --------------------------------------------------------------------------
def answer_valid(answers, w, h):
    """答案自身满足规则 2/3。"""
    if len(set(answers)) != len(answers):
        return False
    for (r, c) in answers:
        if not inb(r, c, w, h):
            return False
    for i in range(len(answers)):
        for j in range(i + 1, len(answers)):
            (r1, c1), (r2, c2) = answers[i], answers[j]
            if r1 == r2 or c1 == c2:
                return False
            if abs(r1 - r2) <= 1 and abs(c1 - c2) <= 1:
                return False
    return True


def dominator_lists(answers, w, h):
    """每格 -> 压制它的答案下标列表（同行/同列/八邻域相邻）。"""
    aset = set(answers)
    out = {}
    for r in range(h):
        for c in range(w):
            if (r, c) in aset:
                continue
            lst = []
            for i, (ar, ac) in enumerate(answers):
                if r == ar or c == ac or (abs(r - ar) <= 1 and abs(c - ac) <= 1):
                    lst.append(i)
            out[(r, c)] = lst
    return out


def answers_cover(answers, w, h):
    """必要条件：每个非答案格至少被一个答案压制。

    否则该格换成自己色块的猫仍是合法解（行列不冲突、八邻域不接触），唯一性必破。
    """
    return all(len(v) >= 1 for v in dominator_lists(answers, w, h).values())


# --------------------------------------------------------------------------
# 第一步：答案坐标（随机排列 + 相邻行 |Δ列| >= 2）
# --------------------------------------------------------------------------
def generate_answers(w, h, k, rng, tries=400):
    """随机生成 k 个满足规则 2/3 的坐标，并要求全盘被压制（必要条件）。"""
    for _ in range(tries):
        rows = list(range(h))
        rng.shuffle(rows)
        rows = sorted(rows[:k])
        cols = list(range(w))
        chosen = []
        used = set()

        def bt(idx):
            if idx == k:
                return True
            r = rows[idx]
            cand = [c for c in cols if c not in used]
            rng.shuffle(cand)
            for c in cand:
                if chosen and rows[idx - 1] == r - 1 and abs(c - chosen[-1][1]) < 2:
                    continue
                ok = True
                for (pr, pc) in chosen:
                    if abs(pr - r) <= 1 and abs(pc - c) <= 1:
                        ok = False
                        break
                if not ok:
                    continue
                chosen.append((r, c))
                used.add(c)
                if bt(idx + 1):
                    return True
                used.discard(c)
                chosen.pop()
            return False

        if bt(0) and answers_cover(chosen, w, h):
            return sorted(chosen)
    return None


# --------------------------------------------------------------------------
# 第二步：色块划分（随机多源 BFS 生长）
# --------------------------------------------------------------------------
def grow_partition(w, h, seeds, rng):
    """以答案格为种子做随机优先队列多源生长：覆盖全盘、每块连通且含且仅含自己的种子。"""
    import heapq
    k = len(seeds)
    owner = [[-1] * w for _ in range(h)]
    heap = []
    for i, (r, c) in enumerate(seeds):
        owner[r][c] = i
        heapq.heappush(heap, (rng.random(), r, c, i))
    while heap:
        _, r, c, i = heapq.heappop(heap)
        for dr, dc in NEIGH4:
            nr, nc = r + dr, c + dc
            if inb(nr, nc, w, h) and owner[nr][nc] == -1:
                owner[nr][nc] = i
                heapq.heappush(heap, (rng.random(), nr, nc, i))
    return owner


def regions_of(owner, w, h):
    reg = [[] for _ in range(max(max(row) for row in owner) + 1)]
    for r in range(h):
        for c in range(w):
            reg[owner[r][c]].append((r, c))
    return reg


def region_connected(cells):
    if not cells:
        return False
    S = set(cells)
    start = cells[0]
    seen = {start}
    dq = deque([start])
    while dq:
        r, c = dq.popleft()
        for dr, dc in NEIGH4:
            nb = (r + dr, c + dc)
            if nb in S and nb not in seen:
                seen.add(nb)
                dq.append(nb)
    return len(seen) == len(S)


def region_connected_without(region_cells, cell):
    rest = [x for x in region_cells if x != cell]
    return region_connected(rest)


# --------------------------------------------------------------------------
# 第三步：全盘枚举（生成阶段用：按色块 MRV + 前向检查）
# --------------------------------------------------------------------------
class _NodeCap(Exception):
    pass


def count_solutions(owner, w, h, k, cap=2, node_budget=COUNT_NODE_BUDGET, collect=0):
    """返回 (解数, 节点数, 是否搜索完成, 收集到的解)。"""
    reg = regions_of(owner, w, h)
    rows = [-1] * h
    cols = [-1] * w
    assigned = [False] * k
    st = {'count': 0, 'nodes': 0, 'done': True, 'sols': []}

    def valid_cells(cells):
        out = []
        for (r, c) in cells:
            if rows[r] != -1 or cols[c] != -1:
                continue
            if r > 0 and rows[r - 1] != -1 and abs(rows[r - 1] - c) <= 1:
                continue
            if r < h - 1 and rows[r + 1] != -1 and abs(rows[r + 1] - c) <= 1:
                continue
            out.append((r, c))
        return out

    def dfs(depth, acc):
        st['nodes'] += 1
        if st['nodes'] > node_budget:
            st['done'] = False
            raise _NodeCap
        if depth == k:
            st['count'] += 1
            if collect and len(st['sols']) < collect:
                st['sols'].append(tuple(acc))
            return st['count'] >= cap
        if sum(1 for x in rows if x == -1) < k - depth:
            return False
        if sum(1 for x in cols if x == -1) < k - depth:
            return False
        best = -1
        best_len = 1 << 30
        best_list = None
        for i in range(k):
            if assigned[i]:
                continue
            lst = valid_cells(reg[i])
            if not lst:
                return False
            if len(lst) < best_len:
                best_len = len(lst)
                best = i
                best_list = lst
                if best_len == 1:
                    break
        for (r, c) in best_list:
            assigned[best] = True
            rows[r] = c
            cols[c] = r
            acc.append((r, c))
            stop = dfs(depth + 1, acc)
            acc.pop()
            assigned[best] = False
            rows[r] = -1
            cols[c] = -1
            if stop:
                return True
        return False

    try:
        dfs(0, [])
    except _NodeCap:
        pass
    return st['count'], st['nodes'], st['done'], st['sols']


# --------------------------------------------------------------------------
# 迁移修复：把非答案解逐个杀掉，直到解数 == 1
# --------------------------------------------------------------------------
def repair_migrate(owner, w, h, k, answers, rng, max_iters=MAX_MIGRATIONS,
                   node_budget=COUNT_NODE_BUDGET):
    """返回 (owner, 迁移次数, 是否达到唯一解)。

    每次迁移都必须: x 不是答案格; A 去掉 x 后仍四连通且仍含 A 的答案格;
    B 是 x 的 4 邻居色块且 B 已含 S 的另一个格子（于是 S 立刻失效）。
    """
    ans_set = set(answers)
    iters = 0
    for it in range(max_iters):
        cnt, nodes, done, sols = count_solutions(owner, w, h, k, cap=2,
                                                 node_budget=node_budget, collect=2)
        if cnt == 1 and done:
            return owner, it, True
        if not done and cnt < 2:
            return owner, it, False
        alt = [s for s in sols if set(s) != ans_set]
        if not alt:
            if cnt < 2:
                return owner, it, (cnt == 1 and done)
            continue
        S = alt[rng.randrange(len(alt))]
        iters = it + 1
        reg = regions_of(owner, w, h)
        cells = [x for x in S if x not in ans_set]
        rng.shuffle(cells)
        moved = False
        for x in cells:
            r0, c0 = x
            A = owner[r0][c0]
            cand = []
            for dr, dc in NEIGH4:
                nr, nc = r0 + dr, c0 + dc
                if not inb(nr, nc, w, h):
                    continue
                B = owner[nr][nc]
                if B == A:
                    continue
                if not any(owner[sr][sc] == B for (sr, sc) in S if (sr, sc) != x):
                    continue
                cand.append(B)
            if not cand:
                continue
            if not region_connected_without(reg[A], x):
                continue
            B = cand[rng.randrange(len(cand))]
            owner[r0][c0] = B
            moved = True
            break
        if not moved:
            # 本轮无法迁移：随机做一次合法边界迁移跳出（仍保证连通）
            flat = [(r, c) for r in range(h) for c in range(w)]
            rng.shuffle(flat)
            for (r0, c0) in flat:
                A = owner[r0][c0]
                if (r0, c0) in ans_set:
                    continue
                opts = []
                for dr, dc in NEIGH4:
                    nr, nc = r0 + dr, c0 + dc
                    if inb(nr, nc, w, h) and owner[nr][nc] != A:
                        opts.append(owner[nr][nc])
                if not opts:
                    continue
                if not region_connected_without(reg[A], (r0, c0)):
                    continue
                owner[r0][c0] = opts[rng.randrange(len(opts))]
                moved = True
                break
            if not moved:
                return owner, it, False
    cnt, nodes, done, _ = count_solutions(owner, w, h, k, cap=2, node_budget=node_budget)
    return owner, max_iters, (cnt == 1 and done)


# --------------------------------------------------------------------------
# 第二套独立实现（--check 用）：按行推进 + 行内列扫描，结构与生成阶段完全不同
# --------------------------------------------------------------------------
def count_solutions_alt(owner, w, h, k, node_budget=6_000_000):
    """独立实现：以行为主序，每行选择"放猫的列"或"不放"，用行列/色块/邻域校验。

    返回 (解数(上限 2), 节点数, 是否完成)。
    """
    cell_region = {}
    region_cells = [[] for _ in range(k)]
    for r in range(h):
        for c in range(w):
            i = owner[r][c]
            cell_region[(r, c)] = i
            region_cells[i].append((r, c))
    row_choices = [[(c, cell_region[(r, c)]) for c in range(w)] for r in range(h)]

    used_col = [False] * w
    used_region = [False] * k
    placed = []
    sols = [0]
    nodes = [0]
    done = [True]

    def conflict(r, c):
        for (pr, pc) in placed:
            if abs(pr - r) <= 1 and abs(pc - c) <= 1:
                return True
        return False

    def feasible(r):
        left = k - len(placed)
        if left > h - r:
            return False
        for i in range(k):
            if used_region[i]:
                continue
            ok = False
            for (rr, cc) in region_cells[i]:
                if rr >= r and not used_col[cc]:
                    ok = True
                    break
            if not ok:
                return False
        return True

    def dfs(r):
        nodes[0] += 1
        if nodes[0] > node_budget:
            done[0] = False
            raise _NodeCap
        if sols[0] >= 2:
            return
        if not feasible(r):
            return
        if r == h:
            if len(placed) == k:
                sols[0] += 1
            return
        must = (h - r) == (k - len(placed))
        if not must:
            dfs(r + 1)
            if sols[0] >= 2:
                return
        for (c, i) in row_choices[r]:
            if used_col[c] or used_region[i] or conflict(r, c):
                continue
            used_col[c] = True
            used_region[i] = True
            placed.append((r, c))
            dfs(r + 1)
            placed.pop()
            used_region[i] = False
            used_col[c] = False
            if sols[0] >= 2:
                return

    try:
        dfs(0)
    except _NodeCap:
        pass
    return sols[0], nodes[0], done[0]


# --------------------------------------------------------------------------
# 难度度量：强制传播 -> 卡住 -> 分支
# --------------------------------------------------------------------------
def propagate(owner, w, h, k):
    """只用必然成立的推理规则做强制传播。

    规则:
      A. 色块只剩 1 个候选格 -> 该格必然是猫。
      B. k == 行数时某行只剩 1 个候选格 -> 该格必然是猫（列同理）。
      C. k == 行数时某色块候选全在同一行 -> 该行其它格排除（列同理）。
      D. 放猫后同行/同列/八邻域全部排除。
    """
    reg = regions_of(owner, w, h)
    excluded = set()
    placed = []
    resolved = set()
    rounds = 0

    def exclude_around(r, c):
        for dr, dc in NEIGH8:
            nr, nc = r + dr, c + dc
            if inb(nr, nc, w, h):
                excluded.add((nr, nc))
        for cc in range(w):
            excluded.add((r, cc))
        for rr in range(h):
            excluded.add((rr, c))

    def candidates(i):
        return [x for x in reg[i] if x not in excluded]

    while True:
        rounds += 1
        changed = False
        for i in range(k):
            if i in resolved:
                continue
            cands = candidates(i)
            if not cands:
                return dict(placed=placed, forced=len(placed), rounds=rounds,
                            excluded=excluded, resolved=resolved,
                            cands={j: candidates(j) for j in range(k)}, contradiction=True)
            if len(cands) == 1:
                r, c = cands[0]
                placed.append((r, c))
                resolved.add(i)
                exclude_around(r, c)
                changed = True
        if changed:
            continue
        if k == h:
            for r in range(h):
                cands = [(r, c) for c in range(w) if (r, c) not in excluded]
                if len(cands) == 1:
                    c = cands[0][1]
                    i = owner[r][c]
                    if i not in resolved:
                        placed.append((r, c))
                        resolved.add(i)
                        exclude_around(r, c)
                        changed = True
        if changed:
            continue
        if k == w:
            for c in range(w):
                cands = [(r, c) for r in range(h) if (r, c) not in excluded]
                if len(cands) == 1:
                    r = cands[0][0]
                    i = owner[r][c]
                    if i not in resolved:
                        placed.append((r, c))
                        resolved.add(i)
                        exclude_around(r, c)
                        changed = True
        if changed:
            continue
        if k == h:
            for i in range(k):
                if i in resolved:
                    continue
                cands = candidates(i)
                if not cands:
                    continue
                rows = {x[0] for x in cands}
                if len(rows) == 1:
                    r = rows.pop()
                    for c in range(w):
                        if (r, c) not in excluded and owner[r][c] != i:
                            excluded.add((r, c))
                            changed = True
        if changed:
            continue
        if k == w:
            for i in range(k):
                if i in resolved:
                    continue
                cands = candidates(i)
                if not cands:
                    continue
                cols = {x[1] for x in cands}
                if len(cols) == 1:
                    c = cols.pop()
                    for r in range(h):
                        if (r, c) not in excluded and owner[r][c] != i:
                            excluded.add((r, c))
                            changed = True
        if not changed:
            break
    return dict(placed=placed, forced=len(placed), rounds=rounds,
                excluded=excluded, resolved=resolved,
                cands={i: candidates(i) for i in range(k)}, contradiction=False)


def search_from_state(owner, w, h, k, excluded, placed, node_budget=200_000):
    """从传播卡住的状态继续 MRV 搜索，度量首个分支宽度与猜测深度。"""
    reg = regions_of(owner, w, h)
    used_rows = {r for (r, c) in placed}
    used_cols = {c for (r, c) in placed}
    used_reg = {owner[r][c] for (r, c) in placed}
    st = {'nodes': 0, 'branch_first': 0, 'done': True, 'found': False}
    plist = list(placed)
    first = [True]

    def cands_of(i):
        out = []
        for (r, c) in reg[i]:
            if (r, c) in excluded or r in used_rows or c in used_cols:
                continue
            bad = False
            for (pr, pc) in plist:
                if abs(pr - r) <= 1 and abs(pc - c) <= 1:
                    bad = True
                    break
            if not bad:
                out.append((r, c))
        return out

    def dfs():
        st['nodes'] += 1
        if st['nodes'] > node_budget:
            st['done'] = False
            raise _NodeCap
        if len(plist) == k:
            st['found'] = True
            return True
        best = None
        best_list = None
        for i in range(k):
            if i in used_reg:
                continue
            lst = cands_of(i)
            if not lst:
                return False
            if best_list is None or len(lst) < len(best_list):
                best = i
                best_list = lst
        if first[0]:
            first[0] = False
            st['branch_first'] = len(best_list)
        for (r, c) in best_list:
            used_rows.add(r)
            used_cols.add(c)
            used_reg.add(best)
            plist.append((r, c))
            if dfs():
                return True
            plist.pop()
            used_reg.discard(best)
            used_cols.discard(c)
            used_rows.discard(r)
        return False

    try:
        dfs()
    except _NodeCap:
        pass
    return st


def measure_difficulty(owner, w, h, k, answers):
    """难度分（纯逻辑强制传播，完全确定、可复现）:

      rounds  只用行/列/色块/邻域排除能连续推出猫的轮数（链越长越难）
      branch  卡住后第一个分支的候选数（候选越少越难）

      difficulty = rounds * 100 - branch

    同一关重复计算必得同一值，不含节点数/耗时等随机或环境相关量。
    """
    prop = propagate(owner, w, h, k)
    forced = prop['forced']
    rounds = prop['rounds']
    branch = 0
    if forced < k and not prop['contradiction']:
        s = search_from_state(owner, w, h, k, prop['excluded'], prop['placed'])
        branch = s['branch_first']
    reg = regions_of(owner, w, h)
    sizes = [len(x) for x in reg]
    difficulty = rounds * 100 - branch
    return dict(forced=forced, rounds=rounds, branch=branch,
                difficulty=int(difficulty), spread=max(sizes) - min(sizes),
                avg_region=round(sum(sizes) / float(len(sizes)), 2),
                contradiction=prop['contradiction'])


# --------------------------------------------------------------------------
# 单关生成 + 轮数调优
# --------------------------------------------------------------------------
def _to_owner_answers(w, h, cells, answer):
    owner = [[cells[r * w + c] for c in range(w)] for r in range(h)]
    answers = [(a // w, a % w) for a in answer]
    return owner, answers


def _candidate(idx, w, h, k, owner, answers, attempt, iters, t0, tuned=0):
    cnt, nodes, done, _ = count_solutions(owner, w, h, k, cap=3)
    if not (cnt == 1 and done):
        return None
    reg = regions_of(owner, w, h)
    if len(reg) != k or any(not region_connected(x) for x in reg):
        return None
    for i in range(k):
        if sum(1 for cell in reg[i] if cell == answers[i]) != 1:
            return None
    m = measure_difficulty(owner, w, h, k, answers)
    return dict(id=idx + 1, w=w, h=h, k=k,
                cells=[owner[r][c] for r in range(h) for c in range(w)],
                answer=[answers[i][0] * w + answers[i][1] for i in range(k)],
                attempts=attempt, iterations=iters,
                difficulty=m['difficulty'], solutions=cnt,
                forced=m['forced'], branch=m['branch'], rounds=m['rounds'],
                tuned=tuned, elapsed=round(time.time() - t0, 3))


def generate_level(idx, w, h, k, seed, max_attempts=MAX_ATTEMPTS,
                   max_migrations=MAX_MIGRATIONS, node_budget=COUNT_NODE_BUDGET):
    """生成一关（随机生长 + 迁移修复 + 唯一解复核）。返回候选 dict 或 None。"""
    t0 = time.time()
    rng = random.Random(seed * 1000003 + idx)
    for attempt in range(1, max_attempts + 1):
        answers = generate_answers(w, h, k, rng)
        if answers is None:
            continue
        owner = grow_partition(w, h, answers, rng)
        owner, iters, ok = repair_migrate(owner, w, h, k, answers, rng,
                                          max_iters=max_migrations,
                                          node_budget=node_budget)
        if not ok:
            continue
        cand = _candidate(idx, w, h, k, owner, answers, attempt, iters, t0)
        if cand:
            return cand
    return None


def tune_rounds(cand, rng, steps=200, target=10 ** 9):
    """在保持唯一解的前提下，用随机边界迁移把强制传播轮数往上推。

    返回 (候选家族列表, 最高轮数)。家族里每个元素对应"沿途某个轮数值"的划分，
    因此可以在同一阶梯内按目标轮数精确挑关，而不必为每个目标重跑一次调优。
    """
    w, h, k = cand['w'], cand['h'], cand['k']
    owner, answers = _to_owner_answers(w, h, cand['cells'], cand['answer'])
    ans_set = set(answers)
    best = [row[:] for row in owner]
    best_r = cand['rounds']
    snaps = [(best_r, [row[:] for row in best])]
    if best_r >= target:
        return [cand], best_r
    cur = [row[:] for row in owner]
    cur_r = best_r
    for _step in range(steps):
        reg = regions_of(cur, w, h)
        cells = [(r, c) for r in range(h) for c in range(w) if (r, c) not in ans_set]
        rng.shuffle(cells)
        moved = False
        for (r0, c0) in cells:
            A = cur[r0][c0]
            opts = [cur[r0 + dr][c0 + dc] for dr, dc in NEIGH4
                    if inb(r0 + dr, c0 + dc, w, h) and cur[r0 + dr][c0 + dc] != A]
            if not opts:
                continue
            if not region_connected_without(reg[A], (r0, c0)):
                continue
            B = opts[rng.randrange(len(opts))]
            cur[r0][c0] = B
            cnt, _n, done, _ = count_solutions(cur, w, h, k, cap=2,
                                               node_budget=COUNT_NODE_BUDGET)
            if cnt == 1 and done:
                rr = propagate(cur, w, h, k)['rounds']
                if rr > cur_r and rr <= target:
                    cur_r = rr
                    best = [row[:] for row in cur]
                    snaps.append((rr, [row[:] for row in best]))
                    moved = True
                else:
                    cur[r0][c0] = A
            else:
                cur[r0][c0] = A
            if moved:
                break
        if cur_r >= target:
            break
    fam = []
    seen_r = set()
    for (rr, grid) in snaps:
        if rr in seen_r:
            continue
        seen_r.add(rr)
        c2 = _candidate(cand['id'] - 1, w, h, k, grid, answers,
                        cand['attempts'], cand['iterations'], 0.0, tuned=rr)
        if c2:
            fam.append(c2)
    if not fam:
        return [cand], best_r
    fam.sort(key=lambda c: c['rounds'])
    return fam, max(c['rounds'] for c in fam)


def _slot_task(slot, w, h, k, seed, tune_target, tune_steps):
    """工作进程任务：生成一个候选；tune_target 非 0 时返回"轮数家族"（列表）。"""
    try:
        cand = generate_level(slot, w, h, k, seed)
        if cand is None:
            return []
        if tune_target:
            rng = random.Random(seed * 7919 + slot * 131 + tune_target)
            fam, _r = tune_rounds(cand, rng, steps=tune_steps, target=tune_target)
            return fam
        return [cand]
    except Exception as exc:  # noqa: BLE001 - 诊断用
        print('  slot %d 生成异常: %r' % (slot + 1, exc), file=sys.stderr)
        return []


# --------------------------------------------------------------------------
# 阶梯内选择：难度升序
# --------------------------------------------------------------------------
def _dp_tier(pools, lo, hi, targets):
    """单个阶梯 DP：从 pools[lo:hi] 各取一个，使 difficulty 非递减且尽量贴近 targets。

    返回 (选中列表, 是否可行)。
    """
    n = hi - lo
    if any(not pools[i] for i in range(lo, hi)):
        return None, False
    INF = float('inf')
    best = [[INF] * len(pools[i]) for i in range(lo, hi)]
    prev_idx = [[-1] * len(pools[i]) for i in range(lo, hi)]
    for j, cand in enumerate(pools[lo]):
        best[0][j] = abs(cand['difficulty'] - targets[0])
    for i in range(1, n):
        for j, cand in enumerate(pools[lo + i]):
            d = cand['difficulty']
            for k2, prev in enumerate(pools[lo + i - 1]):
                if best[i - 1][k2] == INF or prev['difficulty'] > d:
                    continue
                cost = best[i - 1][k2] + abs(d - targets[i])
                if cost < best[i][j]:
                    best[i][j] = cost
                    prev_idx[i][j] = k2
    last = min(range(len(pools[hi - 1])), key=lambda j: best[n - 1][j])
    if best[n - 1][last] == INF:
        return None, False
    chosen = [None] * n
    j = last
    for i in range(n - 1, -1, -1):
        chosen[i] = pools[lo + i][j]
        j = prev_idx[i][j]
    return chosen, True


def _pick_for_slot(pool, prev_d, target_d, cap=10 ** 9):
    """从候选池里挑：优先 [prev_d, cap] 内最接近 target_d 的；否则退而求其次取 >= prev_d 的最小值。"""
    in_range = [c for c in pool if prev_d <= c['difficulty'] <= cap]
    if in_range:
        return min(in_range, key=lambda c: (abs(c['difficulty'] - target_d), c['rounds']))
    cands = [c for c in pool if c['difficulty'] >= prev_d]
    if not cands:
        return None
    return min(cands, key=lambda c: (c['difficulty'], c['rounds']))


def _dp_tier(pools, lo, hi, targets):
    """阶梯内 DP：各关取一个候选，使 difficulty 非递减且尽量贴近 targets。"""
    n = hi - lo
    if any(not pools[i] for i in range(lo, hi)):
        return None
    INF = float('inf')
    best = [[INF] * len(pools[i]) for i in range(lo, hi)]
    prev_idx = [[-1] * len(pools[i]) for i in range(lo, hi)]
    for j, cand in enumerate(pools[lo]):
        best[0][j] = abs(cand['difficulty'] - targets[0])
    for i in range(1, n):
        for j, cand in enumerate(pools[lo + i]):
            d = cand['difficulty']
            row = best[i - 1]
            for k2, prev in enumerate(pools[lo + i - 1]):
                if row[k2] == INF or prev['difficulty'] > d:
                    continue
                cost = row[k2] + abs(d - targets[i])
                if cost < best[i][j]:
                    best[i][j] = cost
                    prev_idx[i][j] = k2
    last = min(range(len(pools[hi - 1])), key=lambda j: best[n - 1][j])
    if best[n - 1][last] == INF:
        return None
    out = [None] * n
    j = last
    for i in range(n - 1, -1, -1):
        out[i] = pools[lo + i][j]
        j = prev_idx[i][j]
    return out


def _groups_of(t):
    lo, hi = t * TIER_SIZE, (t + 1) * TIER_SIZE
    groups = []
    i = lo
    while i < hi:
        j = i
        while j < hi and EFFECTIVE_TABLE[j] == EFFECTIVE_TABLE[i]:
            j += 1
        groups.append((i, j))
        i = j
    return groups


def _tier_capacity(pools, t):
    """阶梯 t 能容纳的最大难度：每个同尺寸组里"第 size 大"的难度取最小。

    这样上一阶梯就不会把难度顶到下一阶梯够不着的高度。
    """
    cap = 10 ** 9
    for (a, b) in _groups_of(t):
        size = b - a
        combined = {}
        for ii in range(a, b):
            for c in pools[ii]:
                combined[(tuple(c['cells']), tuple(c['answer']))] = c
        vals = sorted((c['difficulty'] for c in combined.values()), reverse=True)
        if len(vals) < size:
            return 0
        cap = min(cap, vals[size - 1])
    return cap


def select_levels(pools, verbose=False, filler=None):
    """逐阶梯选关：同一尺寸的几关共用候选池，按 difficulty 升序依次分配。

    这样阶梯内天然非递减；阶梯之间用"抬升阈值 push"把均值一级级顶上去。
    不偷偷改尺寸/k：尺寸仍来自 EFFECTIVE_TABLE。
    """
    chosen = [None] * LEVEL_COUNT
    prev_d = 0
    prev_mean = None
    bad = []
    for t in range(TIER_COUNT):
        lo, hi = t * TIER_SIZE, (t + 1) * TIER_SIZE
        groups = []
        i = lo
        while i < hi:
            j = i
            while j < hi and EFFECTIVE_TABLE[j] == EFFECTIVE_TABLE[i]:
                j += 1
            groups.append((i, j))
            i = j
        cap = _tier_capacity(pools, t + 1) if t + 1 < TIER_COUNT else 10 ** 9
        done = False
        push = 0
        for _attempt in range(8):
            # 阶梯之间只要求"均值抬升"，阶梯内才要求逐关非递减 → 每阶梯从 0 起算
            cur_prev = 0
            picks = []
            ok = True
            for (a, b) in groups:
                size = b - a
                combined = {}
                for ii in range(a, b):
                    for c in pools[ii]:
                        combined[(tuple(c['cells']), tuple(c['answer']))] = c
                cands = sorted(combined.values(),
                               key=lambda c: (c['difficulty'], c['rounds']))
                cands = [c for c in cands if c['difficulty'] >= cur_prev + push]
                if len(cands) < size and filler is not None:
                    need_r = (cur_prev + push) // 100 + 1
                    for ii in range(a, b):
                        newc = filler(ii, need_r)
                        if newc:
                            pools[ii].append(newc)
                            combined[(tuple(newc['cells']), tuple(newc['answer']))] = newc
                    cands = sorted(combined.values(),
                                   key=lambda c: (c['difficulty'], c['rounds']))
                    cands = [c for c in cands if c['difficulty'] >= cur_prev + push]
                if len(cands) < size:
                    ok = False
                    break
                sel = cands[:size]
                picks.append((a, b, sel))
                cur_prev = sel[-1]['difficulty']
            if not ok:
                break
            vals = [c['difficulty'] for (_a, _b, sel) in picks for c in sel]
            mean = sum(vals) / float(len(vals))
            if prev_mean is None or mean > prev_mean:
                for (a, b, sel) in picks:
                    for k2, c in enumerate(sel):
                        chosen[a + k2] = c
                prev_d = cur_prev
                prev_mean = mean
                if verbose:
                    print('  阶梯 %2d 难度: %s 均值 %.0f' % (t + 1, vals, mean))
                done = True
                break
            push += 100
        if not done:
            bad.append(t)
            if verbose:
                print('  阶梯 %2d 失败（候选不够高，push=%d）' % (t + 1, push))
    if bad:
        return None, sorted(set(bad)), None
    return chosen, [], None


def dedupe(pools):
    """去掉候选池里 cells+answer 完全相同的重复题（只保留第一个）。"""
    seen = set()
    for i in range(LEVEL_COUNT):
        keep = []
        for c in pools[i]:
            key = (tuple(c['cells']), tuple(c['answer']))
            if key in seen:
                continue
            seen.add(key)
            keep.append(c)
        pools[i] = keep


def replace_duplicates(chosen, pools):
    """若最终 80 关里有完全相同的关卡（cells 与 answer 都相同），换掉后出现的那一关。

    替换时同时保证：新题不被任何其它关使用，且阶梯内难度仍然非递减。
    """
    keys = [(tuple(c['cells']), tuple(c['answer'])) for c in chosen]
    used = set(keys)
    seen = set()
    replaced = []
    for i, c in enumerate(chosen):
        if keys[i] not in seen:
            seen.add(keys[i])
            continue
        # 找替代：内容未被任何关使用
        for alt in pools[i]:
            k2 = (tuple(alt['cells']), tuple(alt['answer']))
            if k2 in used:
                continue
            ok = True
            if i % TIER_SIZE != 0 and chosen[i - 1]['difficulty'] > alt['difficulty']:
                ok = False
            if ok and i % TIER_SIZE != TIER_SIZE - 1 and \
                    chosen[i + 1]['difficulty'] < alt['difficulty']:
                ok = False
            if not ok:
                continue
            used.discard(keys[i])
            used.add(k2)
            seen.add(k2)
            chosen[i] = alt
            keys[i] = k2
            replaced.append(i + 1)
            break
    return replaced


def _finalize(chosen, pools):
    """最终去重 + 重排 id。"""
    rep = replace_duplicates(chosen, pools)
    for i, c in enumerate(chosen):
        c['id'] = i + 1
    return rep


def build_levels(jobs=1, verbose=True, base_seeds=5, tune_specs=((0, 0), (10 ** 9, 140)),
                 extra_rounds=6):
    """生成 80 关：每槽先做候选池（自然 + 轮数调优），再逐阶梯选难度非递减序列。"""
    import multiprocessing as mp
    table = EFFECTIVE_TABLE
    pools = [[] for _ in range(LEVEL_COUNT)]

    def run(tasks):
        if jobs > 1:
            with mp.Pool(jobs) as pool:
                return pool.starmap(_slot_task, tasks)
        return [_slot_task(*t) for t in tasks]

    def absorb(tasks, outs):
        for t, res in zip(tasks, outs):
            if not res:
                continue
            for c in (res if isinstance(res, list) else [res]):
                pools[t[0]].append(c)
        for i in range(LEVEL_COUNT):
            pools[i].sort(key=lambda c: (c['difficulty'], c['rounds']))

    fill_counter = [0]

    def filler(slot, need_rounds):
        w, h, k = table[slot]
        for _try in range(4):
            fill_counter[0] += 1
            sd = 5000 + fill_counter[0]
            res = _slot_task(slot, w, h, k, sd, 100 * need_rounds, 160)
            if res:
                best = max(res, key=lambda c: c['rounds'])
                if best['rounds'] >= need_rounds:
                    return best
        return None

    t0 = time.time()
    tasks = []
    for slot in range(LEVEL_COUNT):
        w, h, k = table[slot]
        for sd in range(1, base_seeds + 1):
            tasks.append((slot, w, h, k, sd, 0, 0))
        for (tt, st) in tune_specs:
            if tt:
                tasks.append((slot, w, h, k, 900 + (tt if tt < 10 ** 8 else 7), tt, st))
    absorb(tasks, run(tasks))
    if verbose:
        print('  候选池 %.1fs，各槽候选数: %s'
              % (time.time() - t0, [len(p) for p in pools]))
    try:
        with open('/tmp/catcube_pools.json', 'w', encoding='utf-8') as f:
            json.dump(pools, f)
    except Exception:  # noqa: BLE001 - 调试用
        pass

    dedupe(pools)
    replaced = []
    seed_next = 100
    for extra in range(extra_rounds):
        chosen, bad_tiers, means = select_levels(pools, verbose=verbose, filler=filler)
        if chosen is not None:
            replaced = _finalize(chosen, pools)
            return chosen, [], replaced
        if verbose:
            print('  阶梯 %s 需要更多候选（或抬升不足），补生成...'
                  % [t + 1 for t in bad_tiers])
        tasks = []
        for t in bad_tiers:
            for slot in range(t * TIER_SIZE, (t + 1) * TIER_SIZE):
                w, h, k = table[slot]
                for sd in range(seed_next, seed_next + 4):
                    tasks.append((slot, w, h, k, sd, 0, 0))
                # 补一个"高轮数"调优候选，帮助抬升阶梯均值
                tasks.append((slot, w, h, k, seed_next, 10 ** 9, 180))
        seed_next += 4
        absorb(tasks, run(tasks))
    chosen, bad_tiers, means = select_levels(pools, verbose=verbose, filler=filler)
    if chosen is None:
        return None, [t * TIER_SIZE + 1 for t in bad_tiers], replaced
    replaced = _finalize(chosen, pools)
    return chosen, [], replaced


# --------------------------------------------------------------------------
# 产物写出
# --------------------------------------------------------------------------
def c_array(name, values, per_line=20):
    out = ['static const uint8_t %s[] = {' % name]
    for i in range(0, len(values), per_line):
        out.append('    ' + ', '.join(str(v) for v in values[i:i + per_line]) + ',')
    out.append('};')
    return '\n'.join(out)


def write_outputs(levels):
    os.makedirs(os.path.dirname(OUT_C), exist_ok=True)
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    os.makedirs(os.path.dirname(OUT_TXT), exist_ok=True)

    # ---- JSON ----
    data = dict(version=1, count=len(levels), levels=levels)
    with open(OUT_JSON, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=1, sort_keys=False)
        f.write('\n')

    # ---- C 数据 ----
    parts = ['/* 生成物，勿手改。由 tools/gen_catcube_levels.py 生成。 */',
             '#include "app/catcube_levels.h"',
             '']
    for lv in levels:
        parts.append('/* 关卡 %d: %dx%d, k=%d */' % (lv['id'], lv['w'], lv['h'], lv['k']))
        parts.append(c_array('c%d' % lv['id'], lv['cells']))
        parts.append(c_array('a%d' % lv['id'], lv['answer']))
        parts.append('')
    parts.append('const catcube_level_def_t CATCUBE_LEVELS[] = {')
    for lv in levels:
        parts.append('    {%d, %d, %d, c%d, a%d},' % (lv['w'], lv['h'], lv['k'], lv['id'], lv['id']))
    parts.append('};')
    parts.append('')
    parts.append('const int CATCUBE_LEVEL_COUNT = %d;' % len(levels))
    parts.append('')
    with open(OUT_C, 'w', encoding='utf-8') as f:
        f.write('\n'.join(parts))

    # ---- 中文清单 ----
    lines = []
    for n, lv in enumerate(levels):
        w, h, k = lv['w'], lv['h'], lv['k']
        lines.append('关卡序号：%d' % lv['id'])
        lines.append('棋盘尺寸：%d × %d' % (w, h))
        lines.append('目标放置猫咪数量：%d' % k)
        lines.append('颜色网格：')
        for r in range(h):
            lines.append(' '.join(str(lv['cells'][r * w + c]) for c in range(w)))
        coords = []
        for idx in sorted(lv['answer']):
            coords.append('(%d,%d)' % (idx // w, idx % w))
        lines.append('猫咪标准答案坐标：' + ', '.join(coords))
        if n != len(levels) - 1:
            lines.append('')
    with open(OUT_TXT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    return OUT_JSON, OUT_C, OUT_TXT


# --------------------------------------------------------------------------
# --check：独立复核已有产物
# --------------------------------------------------------------------------
def parse_c_file(path, expected):
    """从生成物 C 文件里抽回每关 cells/answer，与 JSON 对照。"""
    import re
    src = open(path, encoding='utf-8').read()
    arrays = {}
    for m in re.finditer(r'static const uint8_t (\w+)\[\] = \{(.*?)\};', src, re.S):
        name = m.group(1)
        vals = [int(x) for x in re.findall(r'\d+', m.group(2))]
        arrays[name] = vals
    entries = re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\w+),\s*(\w+)\s*\}', src)
    count_m = re.search(r'CATCUBE_LEVEL_COUNT\s*=\s*(\d+)\s*;', src)
    got = []
    for (w, h, k, cn, an) in entries:
        got.append(dict(w=int(w), h=int(h), k=int(k),
                        cells=arrays[cn], answer=arrays[an]))
    return got, (int(count_m.group(1)) if count_m else None)


def parse_txt_file(path):
    """从中文清单里抽回每关数据。"""
    text = open(path, encoding='utf-8').read()
    blocks = [b for b in text.strip().split('\n\n') if b.strip()]
    out = []
    for b in blocks:
        lines = [x for x in b.strip().split('\n') if x.strip() != '']
        rec = {}
        grid = []
        in_grid = False
        for line in lines:
            if line.startswith('关卡序号：'):
                rec['id'] = int(line.split('：')[1])
                in_grid = False
            elif line.startswith('棋盘尺寸：'):
                dims = line.split('：')[1].split('×')
                rec['w'] = int(dims[0].strip())
                rec['h'] = int(dims[1].strip())
                in_grid = False
            elif line.startswith('目标放置猫咪数量：'):
                rec['k'] = int(line.split('：')[1])
                in_grid = False
            elif line.startswith('颜色网格：'):
                in_grid = True
            elif line.startswith('猫咪标准答案坐标：'):
                in_grid = False
                body = line.split('：', 1)[1]
                coords = []
                for part in body.split('),'):
                    part = part.replace('(', '').replace(')', '').strip()
                    if not part:
                        continue
                    a, bb = part.split(',')
                    coords.append((int(a), int(bb)))
                rec['coords'] = coords
            elif in_grid:
                grid.append([int(x) for x in line.split()])
        rec['grid'] = grid
        out.append(rec)
    return out


def _tier_means(levels):
    out = []
    for t in range(TIER_COUNT):
        seq = [levels[t * TIER_SIZE + j]['difficulty'] for j in range(TIER_SIZE)]
        out.append(sum(seq) / float(len(seq)))
    return out


def check_all(verbose=True):
    """独立复核：连通性 BFS、每块恰好 1 个答案、答案合法、解数 == 1（第二套枚举）。"""
    if not os.path.exists(OUT_JSON):
        print('缺少 %s' % OUT_JSON)
        return False
    data = json.load(open(OUT_JSON, encoding='utf-8'))
    levels = data['levels']
    ok_all = True
    problems = []
    max_nodes = 0
    total_time = 0.0

    if len(levels) != LEVEL_COUNT:
        problems.append('关卡数量 %d != %d' % (len(levels), LEVEL_COUNT))
        ok_all = False

    for i, lv in enumerate(levels):
        idx = i + 1
        w, h, k = lv['w'], lv['h'], lv['k']
        exp = EFFECTIVE_TABLE[i]
        if (w, h, k) != exp:
            problems.append('关卡 %d 尺寸/k 与阶梯表不符: %s != %s' % (idx, (w, h, k), exp))
            ok_all = False
        cells = lv['cells']
        ans_idx = lv['answer']
        if len(cells) != w * h:
            problems.append('关卡 %d cells 长度 %d != %d' % (idx, len(cells), w * h))
            ok_all = False
            continue
        if len(ans_idx) != k:
            problems.append('关卡 %d answer 长度 %d != %d' % (idx, len(ans_idx), k))
            ok_all = False
            continue
        if sorted(set(cells)) != list(range(k)):
            problems.append('关卡 %d 色块 id 不是 0..%d' % (idx, k - 1))
            ok_all = False
        owner = [[cells[r * w + c] for c in range(w)] for r in range(h)]
        answers = [(a // w, a % w) for a in ans_idx]
        # 1) 每块恰好 1 个答案格
        per_region = [0] * k
        for (r, c) in answers:
            per_region[owner[r][c]] += 1
        if any(v != 1 for v in per_region):
            problems.append('关卡 %d 存在色块答案数 != 1: %s' % (idx, per_region))
            ok_all = False
        # 2) 每块四连通（独立 BFS 实现）
        for rid in range(k):
            cells_r = [(r, c) for r in range(h) for c in range(w) if owner[r][c] == rid]
            if not cells_r:
                problems.append('关卡 %d 色块 %d 为空' % (idx, rid))
                ok_all = False
                continue
            S = set(cells_r)
            start = cells_r[0]
            seen = {start}
            stack = [start]
            while stack:
                r, c = stack.pop()
                for dr, dc in NEIGH4:
                    nb = (r + dr, c + dc)
                    if nb in S and nb not in seen:
                        seen.add(nb)
                        stack.append(nb)
            if len(seen) != len(S):
                problems.append('关卡 %d 色块 %d 不四连通' % (idx, rid))
                ok_all = False
        # 3) 答案满足规则 2/3
        if not answer_valid(answers, w, h):
            problems.append('关卡 %d 答案违反规则 2/3' % idx)
            ok_all = False
        # 4) 解数 == 1（第二套独立实现）
        t0 = time.time()
        cnt, nodes, done = count_solutions_alt(owner, w, h, k)
        total_time += time.time() - t0
        max_nodes = max(max_nodes, nodes)
        if not done:
            problems.append('关卡 %d 独立枚举未完成（节点上限）' % idx)
            ok_all = False
        if cnt != 1:
            problems.append('关卡 %d 解数 %d != 1' % (idx, cnt))
            ok_all = False
        lv['_check_solutions'] = cnt
        lv['_check_nodes'] = nodes
        # 5) difficulty 必须等于 rounds*100 - branch（纯逻辑传播，确定性复算）
        m = measure_difficulty(owner, w, h, k, answers)
        if m['rounds'] != lv['rounds'] or m['branch'] != lv['branch']:
            problems.append('关卡 %d rounds/branch 与数据不符: 数据(%d,%d) 复算(%d,%d)'
                            % (idx, lv['rounds'], lv['branch'], m['rounds'], m['branch']))
            ok_all = False
        if lv['difficulty'] != m['difficulty']:
            problems.append('关卡 %d difficulty 与 rounds*100-branch 不符: %d != %d'
                            % (idx, lv['difficulty'], m['difficulty']))
            ok_all = False

    # 4b) 关卡不重复（cells 与 answer 都相同）
    seen = {}
    for i, lv in enumerate(levels):
        key = (tuple(lv['cells']), tuple(lv['answer']))
        if key in seen:
            problems.append('关卡 %d 与关卡 %d 完全相同' % (i + 1, seen[key] + 1))
            ok_all = False
        else:
            seen[key] = i

    # 4c) 阶梯表：L1-L20 与原表逐关一致；L21-L80 每阶梯尺寸多重集一致、
    #     至少两种尺寸、尺寸 8..14、k=n
    for t in range(TIER_COUNT):
        lo, hi = t * TIER_SIZE, (t + 1) * TIER_SIZE
        got = [(levels[i]['w'], levels[i]['h'], levels[i]['k']) for i in range(lo, hi)]
        exp = LEVEL_TABLE[lo:hi]
        if t < 2:
            if got != exp:
                problems.append('阶梯 %d 尺寸顺序与生效表不一致' % (t + 1))
                ok_all = False
        else:
            if sorted(got) != sorted(exp):
                problems.append('阶梯 %d 尺寸多重集与阶梯表不一致' % (t + 1))
                ok_all = False
            sizes = {g[0] for g in got}
            if len(sizes) < 2:
                problems.append('阶梯 %d 只有一种尺寸' % (t + 1))
                ok_all = False
            for (w2, h2, k2) in got:
                if w2 != h2 or k2 != w2 or not (8 <= w2 <= 14):
                    problems.append('阶梯 %d 出现非法尺寸 %s' % (t + 1, (w2, h2, k2)))
                    ok_all = False
                    break

    # 5) 难度阶梯：阶梯内非递减 + 相邻阶梯均值抬升
    means = []
    for t in range(TIER_COUNT):
        seq = [levels[t * TIER_SIZE + j]['difficulty'] for j in range(TIER_SIZE)]
        for j in range(1, len(seq)):
            if seq[j] < seq[j - 1]:
                problems.append('阶梯 %d 内难度非单调不降: %s' % (t + 1, seq))
                ok_all = False
                break
        means.append(sum(seq) / float(len(seq)))
    for t in range(1, TIER_COUNT):
        if means[t] <= means[t - 1]:
            problems.append('阶梯 %d 均值 %.1f 未抬升于阶梯 %d 均值 %.1f'
                            % (t + 1, means[t], t, means[t - 1]))
            ok_all = False

    # 6) C 数据与 JSON 一致
    if os.path.exists(OUT_C):
        got, cnt = parse_c_file(OUT_C, len(levels))
        if cnt != len(levels):
            problems.append('C 文件 CATCUBE_LEVEL_COUNT=%s != %d' % (cnt, len(levels)))
            ok_all = False
        if len(got) != len(levels):
            problems.append('C 文件关卡条目数 %d != %d' % (len(got), len(levels)))
            ok_all = False
        for i, (g, lv) in enumerate(zip(got, levels)):
            if (g['w'], g['h'], g['k']) != (lv['w'], lv['h'], lv['k']) or \
               g['cells'] != lv['cells'] or g['answer'] != lv['answer']:
                problems.append('C 文件关卡 %d 与 JSON 不一致' % (i + 1))
                ok_all = False
    else:
        problems.append('缺少 %s' % OUT_C)
        ok_all = False

    # 7) 中文清单与 JSON 一致
    if os.path.exists(OUT_TXT):
        recs = parse_txt_file(OUT_TXT)
        if len(recs) != len(levels):
            problems.append('中文清单关卡数 %d != %d' % (len(recs), len(levels)))
            ok_all = False
        for i, (rec, lv) in enumerate(zip(recs, levels)):
            w, h, k = lv['w'], lv['h'], lv['k']
            grid = [[lv['cells'][r * w + c] for c in range(w)] for r in range(h)]
            if rec.get('w') != w or rec.get('h') != h or rec.get('k') != k or \
               rec.get('id') != lv['id'] or rec.get('grid') != grid:
                problems.append('中文清单关卡 %d 与 JSON 不一致' % (i + 1))
                ok_all = False
            coords = [(a // w, a % w) for a in sorted(lv['answer'])]
            if rec.get('coords') != coords:
                problems.append('中文清单关卡 %d 坐标与 JSON 不一致' % (i + 1))
                ok_all = False
    else:
        problems.append('缺少 %s' % OUT_TXT)
        ok_all = False

    if verbose:
        print('== --check 结果 ==')
        print('关卡数: %d' % len(levels))
        print('独立枚举(第二套实现)总耗时: %.2fs, 单关最大节点: %d' % (total_time, max_nodes))
        print('阶梯难度均值: %s' % ['%.1f' % m for m in _tier_means(levels)])
        if problems:
            print('FAIL: %d 个问题' % len(problems))
            for p in problems[:40]:
                print('  -', p)
        else:
            print('PASS: 全部 80 关色块连通、每块恰好 1 个答案、答案合法、解数 == 1；'
                  'C/JSON/TXT 三方一致；阶梯难度单调不降')
    return ok_all, problems, levels


def probe():
    """可行性/耗时探测。"""
    print('== probe ==')
    for (w, h, k) in sorted(set(LEVEL_TABLE)):
        t0 = time.time()
        res = generate_level(0, w, h, k, 1, max_attempts=3, max_migrations=400)
        dt = time.time() - t0
        if res:
            print('%2dx%-2d k=%-2d OK attempts=%d iters=%d diff=%d forced=%d branch=%d %.2fs'
                  % (w, h, k, res['attempts'], res['iterations'], res['difficulty'],
                     res['forced'], res['branch'], dt))
        else:
            print('%2dx%-2d k=%-2d FAIL %.2fs' % (w, h, k, dt))


def main(argv=None):
    ap = argparse.ArgumentParser(description='放猫咪关卡生成器（逆向生成法）')
    ap.add_argument('--check', action='store_true', help='只校验已有产物，不重写')
    ap.add_argument('--probe', action='store_true', help='可行性探测')
    ap.add_argument('--report', action='store_true', help='打印逐关报告')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 1)))
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--select-only', action='store_true',
                    help='用 /tmp/catcube_pools.json 离线跑选关逻辑（调试）')
    ap.add_argument('--from-pools', action='store_true',
                    help='用 /tmp/catcube_pools.json 直接选关并写出产物（调试/复现）')
    args = ap.parse_args(argv)

    if args.check:
        ok, problems, levels = check_all()
        return 0 if ok else 1
    if args.probe:
        probe()
        return 0
    if args.from_pools:
        pools = json.load(open('/tmp/catcube_pools.json', encoding='utf-8'))
        chosen, bad, _m = select_levels(pools, verbose=True, filler=None)
        if chosen is None:
            print('不可行阶梯: %s' % [t + 1 for t in bad])
            return 1
        replaced = _finalize(chosen, pools)
        if replaced:
            print('去重替换的关卡: %s' % replaced)
        write_outputs(chosen)
        print('写出: %s' % OUT_JSON)
        print('写出: %s' % OUT_C)
        print('写出: %s' % OUT_TXT)
        ok, problems, _ = check_all()
        return 0 if ok else 1
    if args.select_only:
        pools = json.load(open('/tmp/catcube_pools.json', encoding='utf-8'))
        chosen, bad, _m = select_levels(pools, verbose=True, filler=None)
        if chosen is None:
            print('不可行阶梯: %s' % [t + 1 for t in bad])
            for t in bad:
                for i in range(t * TIER_SIZE, (t + 1) * TIER_SIZE):
                    print('  slot %d %s pool=%s'
                          % (i + 1, EFFECTIVE_TABLE[i],
                             sorted(c['difficulty'] for c in pools[i])))
            return 1
        for t in range(TIER_COUNT):
            print('阶梯 %d: %s' % (t + 1, [chosen[t * TIER_SIZE + j]['difficulty']
                                          for j in range(TIER_SIZE)]))
        return 0
    if args.report:
        if not os.path.exists(OUT_JSON):
            print('缺少 %s' % OUT_JSON)
            return 1
        data = json.load(open(OUT_JSON, encoding='utf-8'))
        levels = data['levels']
        print('关卡 尺寸   k  尝试  迁移  rounds branch 难度 解数')
        for lv in levels:
            print('L%-3d %2dx%-2d %2d %5d %5d %6d %6d %5d %4d'
                  % (lv['id'], lv['w'], lv['h'], lv['k'], lv['attempts'],
                     lv['iterations'], lv['rounds'], lv['branch'],
                     lv['difficulty'], lv['solutions']))
        print()
        for t in range(TIER_COUNT):
            seq = [levels[t * TIER_SIZE + j]['difficulty'] for j in range(TIER_SIZE)]
            print('阶梯 %d: %s 均值 %.0f' % (t + 1, seq, sum(seq) / float(len(seq))))
        print('attempts 最大 %d, iterations 最大 %d'
              % (max(lv['attempts'] for lv in levels),
                 max(lv['iterations'] for lv in levels)))
        return 0

    t0 = time.time()
    print('生成 80 关（jobs=%d）...' % args.jobs)
    results, missing, replaced = build_levels(jobs=args.jobs)
    if missing:
        print('!! 以下关卡生成失败: %s' % missing)
        return 2
    if replaced:
        print('去重替换的关卡: %s' % replaced)
    write_outputs(results)
    print('写出: %s' % OUT_JSON)
    print('写出: %s' % OUT_C)
    print('写出: %s' % OUT_TXT)
    print('总耗时 %.1fs' % (time.time() - t0))
    print('各关 attempts 最大 %d，iterations 最大 %d'
          % (max(r['attempts'] for r in results), max(r['iterations'] for r in results)))
    ok, problems, _ = check_all()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

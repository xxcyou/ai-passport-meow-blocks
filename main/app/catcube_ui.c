// main/app/catcube_ui.c —— 「猫猫方块」全部界面:关卡列表、自绘棋盘、通关演出、结算。
//
// 为什么自绘:这块板子只有 240x320 屏和 3 个按键,一关最多 14x14=196 格。用 196 个
// LVGL 控件画棋盘会吃掉几十 KB 堆(LVGL 内存池只有 32KB),还会让每次移动光标都触发
// 一堆布局计算。这里改成"一个对象 + 一个 DRAW_MAIN 回调":格子、像素猫、像素叉、
// 光标、生命、星级、进度条全部在回调里用 lv_draw_rect 直接画。
//
// 线程模型:本文件所有函数都在持 bsp_lvgl_lock() 的上下文执行(输入任务 / 启动流程),
// 因此绘制回调读到的 state 不会被并发改写。
#include "app/catcube_ui.h"

#include "app/app_fonts.h"
#include "app/app_strings.h"
#include "app/catcube_audio.h"
#include "app/catcube_levels.h"
#include "app/catcube_logic.h"
#include "bsp_battery.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 版面常量(240x320)
// ---------------------------------------------------------------------------
#define UI_W 240
#define UI_H 320

#define PLAY_BOARD_X 8
#define PLAY_BOARD_Y 54
#define PLAY_BOARD_MAX 224
#define PLAY_CELL_MAX 34

#define LEVEL_COLS 8
#define LEVEL_CELL 24
#define LEVEL_GAP 2
#define LEVEL_GRID_X 8
#define LEVEL_GRID_Y 58
#define LEVEL_PAGE_SIZE (LEVEL_COLS * 10)

#define CARD_W 204

#define CLR_BG 0x101820u
#define CLR_TEXT 0xF2F4F5u
#define CLR_TEXT_DIM 0x9FB0B8u
#define CLR_ACCENT 0xFFC107u
#define CLR_CLEARED 0x2E7D32u
#define CLR_UNLOCKED 0x37474Fu
#define CLR_LOCKED 0x1B2429u
#define CLR_DANGER 0xFF1744u
#define CLR_CARD 0x263238u
#define CLR_CAT 0x241C18u
#define CLR_CAT_EYE 0xFFF3E0u
#define CLR_X 0x263238u
#define CLR_X_AUTO 0x2B3A42u
#define CLR_HEART 0xFF5252u
#define CLR_HEART_OFF 0x3A2A2Cu
#define CLR_STAR 0xFFD54Fu
#define CLR_STAR_OFF 0x3A3A3Au

// 14 种色块底色:用"最远点采样"在 HSV 网格里挑出来的,两两加权色差 >= 226
// (旧版最小色差只有 44,红/粉几块几乎分不清)。全部保持中高亮度,
// 深色的猫头和叉压在上面都看得清。
static const uint32_t REGION_COLORS[CATCUBE_MAX_SIDE] = {
    0xE87878u, 0x38FFFFu, 0xD8AB2Fu, 0x2F89D8u, 0xBFF235u, 0x6F6FF2u, 0x2FD82Fu,
    0xBB56FFu, 0x61FF56u, 0xFF93FFu, 0x85E5ABu, 0xFFCCB2u, 0x4EC7E5u, 0x8CA7F2u,
};

// 8x8 像素素材:1 = 主色,2 = 高光。
static const uint8_t CAT_SPRITE[64] = {
    0, 1, 0, 0, 0, 0, 1, 0,
    1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 0,
};

static const uint8_t X_SPRITE[64] = {
    1, 0, 0, 0, 0, 0, 0, 1,
    0, 1, 0, 0, 0, 0, 1, 0,
    0, 0, 1, 0, 0, 1, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 0, 0,
    0, 1, 0, 0, 0, 0, 1, 0,
    1, 0, 0, 0, 0, 0, 0, 1,
};

static const uint8_t HEART_SPRITE[64] = {
    0, 1, 1, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint8_t STAR_SPRITE[64] = {
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 1, 1, 1, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 0, 1, 1, 0, 0,
    1, 1, 0, 0, 0, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

typedef enum { PAGE_LEVELS = 0, PAGE_PLAY } cat_page_t;
typedef enum { OV_NONE = 0, OV_MENU, OV_WIN, OV_FAIL, OV_TUTORIAL, OV_VOLUME } cat_overlay_t;
typedef enum { WIN_IDLE = 0, WIN_SCALE, WIN_ROTATE, WIN_BAR, WIN_DONE } win_phase_t;

#define MENU_ITEMS 6 // 一页最多 6 行(两级菜单共用这 6 个行对象)
#define MENU_MAIN_ITEMS 6
#define MENU_SET_ITEMS  6
#define WIN_ITEMS  2

typedef struct {
    const catcube_ui_hooks_t *hooks;

    lv_obj_t *lv_page;
    lv_obj_t *lv_title;
    lv_obj_t *lv_progress;
    lv_obj_t *lv_page_range;
    lv_obj_t *lv_battery;
    lv_obj_t *lv_grid;

    lv_obj_t *pl_page;
    lv_obj_t *pl_title;
    lv_obj_t *pl_size;
    lv_obj_t *pl_cats;
    lv_obj_t *pl_timer;
    lv_obj_t *pl_battery;
    lv_obj_t *pl_board;
    lv_obj_t *pl_hint;
    lv_obj_t *pl_hud; // 生命(自绘)

    lv_obj_t *ov_bg;
    lv_obj_t *card_menu;
    lv_obj_t *menu_row[MENU_ITEMS];
    lv_obj_t *menu_label[MENU_ITEMS];
    bool tutorial_from_menu; // 从菜单打开:关掉时回菜单,底部提示改成"OK 返回"

    lv_obj_t *win_page; // 通关演出 + 结算(自绘)
    lv_obj_t *win_row[WIN_ITEMS];

    lv_obj_t *tut_page; // 新手教学(前 5 关)
    lv_obj_t *vol_page; // 音量
    bool reset_armed;
    lv_obj_t *fail_page; // 失败
    lv_obj_t *fail_row[WIN_ITEMS];

    cat_page_t page;
    cat_overlay_t overlay;
    bool sel_initialized;
    int level; // 1 起
    catcube_state_t st;
    int sel;
    int menu_sel;
    int menu_page; // 0 = 主菜单,1 = 设置
    int win_sel;
    int board_side;
    int cell;

    uint32_t clock_ms;
    uint32_t limit_ms;
    uint32_t final_ms; // 通关/失败那一刻的用时,演出和结算都用它,不再跟时钟跑
    uint32_t shake_ms; // 猜错红叉的抖动剩余时间
    int shake_idx;
    uint32_t sweep_ms; // 确认猫之后"自动排除"从猫头向外扩散的动画时间
    int sweep_r;
    int sweep_c;
    bool sweep_active;
    uint32_t toast_ms; // 底部提示气泡的剩余显示时间

    uint32_t level_serial;
    win_phase_t win_phase;
    uint32_t win_ms;
    int win_bar;
    int win_stars;
} cat_ui_t;

static cat_ui_t s_ui;

static void draw_text(lv_layer_t *layer, int x, int y, int w, int h, const char *text,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align);

// ---------------------------------------------------------------------------
// 绘制原语
// ---------------------------------------------------------------------------
static void fill_rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color, int radius,
                      lv_opa_t opa) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opa;
    dsc.radius = radius;
    lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + w - 1, .y2 = y + h - 1};
    lv_draw_rect(layer, &dsc, &area);
}

static void stroke_rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color, int width,
                        int radius) {
    if (w <= 0 || h <= 0 || width <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(color);
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_width = width;
    dsc.radius = radius;
    lv_area_t area = {.x1 = x, .y1 = y, .x2 = x + w - 1, .y2 = y + h - 1};
    lv_draw_rect(layer, &dsc, &area);
}

// 按 pct% 把 a 往 b 混合:用来给方块做"上亮下暗"的立体边,不需要真 3D。
static uint32_t mix_color(uint32_t a, uint32_t b, int pct) {
    const int ar = (int)((a >> 16) & 0xFFu), ag = (int)((a >> 8) & 0xFFu), ab = (int)(a & 0xFFu);
    const int br = (int)((b >> 16) & 0xFFu), bg = (int)((b >> 8) & 0xFFu), bb = (int)(b & 0xFFu);
    const int r = ar + (br - ar) * pct / 100;
    const int g = ag + (bg - ag) * pct / 100;
    const int bl = ab + (bb - ab) * pct / 100;
    return ((uint32_t)(r & 0xFF) << 16) | ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(bl & 0xFF);
}

static void draw_text(lv_layer_t *layer, int x, int y, int w, int h, const char *text,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align) {
    if (!text || !font) return;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.align = align;
    dsc.text_local = 0;
    const int top = y + (h - (int)font->line_height) / 2;
    lv_area_t area = {.x1 = x, .y1 = top, .x2 = x + w - 1, .y2 = top + (int)font->line_height};
    lv_draw_label(layer, &dsc, &area);
}

// 8x8 像素图。scale_pct: 100..150;quarter: 顺时针 90 度的次数(0..3)。
// 同色横向连续像素合并成一个矩形;旋转时无法合并,就逐像素画(只在通关演出里用)。
static void draw_sprite(lv_layer_t *layer, int x, int y, int cell, const uint8_t *pat,
                        uint32_t body, uint32_t accent, int scale_pct, int quarter) {
    int px = cell / 8;
    if (px < 1) px = 1;
    if (scale_pct > 100) px = (px * scale_pct + 99) / 100;
    const int side = px * 8;
    const int ox = x + (cell - side) / 2;
    const int oy = y + (cell - side) / 2;
    quarter &= 3;

    if (quarter == 0) {
        for (int r = 0; r < 8; r++) {
            int c = 0;
            while (c < 8) {
                const uint8_t v = pat[r * 8 + c];
                if (v == 0) {
                    c++;
                    continue;
                }
                int e = c;
                while (e < 8 && pat[r * 8 + e] == v) e++;
                fill_rect(layer, ox + c * px, oy + r * px, (e - c) * px, px,
                          v == 2 ? accent : body, 0, LV_OPA_COVER);
                c = e;
            }
        }
        return;
    }

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            const uint8_t v = pat[r * 8 + c];
            if (v == 0) continue;
            int rr = r;
            int cc = c;
            for (int q = 0; q < quarter; q++) {
                const int t = rr;
                rr = cc;
                cc = 7 - t;
            }
            fill_rect(layer, ox + cc * px, oy + rr * px, px, px, v == 2 ? accent : body, 0,
                      LV_OPA_COVER);
        }
    }
}

// 半星:左半边填色,右半边暗色。
static void draw_star(lv_layer_t *layer, int x, int y, int cell, int half) {
    int px = cell / 8;
    if (px < 1) px = 1;
    const int side = px * 8;
    const int ox = x + (cell - side) / 2;
    const int oy = y + (cell - side) / 2;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (!STAR_SPRITE[r * 8 + c]) continue;
            uint32_t color = CLR_STAR_OFF;
            if (half >= 2) color = CLR_STAR;
            else if (half == 1 && c < 4) color = CLR_STAR;
            fill_rect(layer, ox + c * px, oy + r * px, px, px, color, 0, LV_OPA_COVER);
        }
    }
}

// 逐位画数字,避免"延后绘制 + 栈缓冲"的悬空指针问题。
static const char *const DIGIT_TEXT[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

static void draw_number(lv_layer_t *layer, int x, int y, int w, int h, int value,
                        const lv_font_t *font, uint32_t color, lv_text_align_t align) {
    char digits[8];
    int n = 0;
    if (value <= 0) {
        digits[n++] = '0';
    } else {
        char rev[8];
        int r = 0;
        while (value > 0 && r < 7) {
            rev[r++] = (char)('0' + value % 10);
            value /= 10;
        }
        while (r > 0) digits[n++] = rev[--r];
    }
    const int digit_w = 8;
    const int total = n * digit_w;
    int dx = (align == LV_TEXT_ALIGN_RIGHT) ? (x + w - total)
             : (align == LV_TEXT_ALIGN_CENTER) ? (x + (w - total) / 2)
                                               : x;
    for (int i = 0; i < n; i++) {
        draw_text(layer, dx + i * digit_w, y, digit_w, h, DIGIT_TEXT[digits[i] - '0'], font, color,
                  LV_TEXT_ALIGN_CENTER);
    }
}

// 关卡格子底部的三星条:每颗星 2 个半星,半星画成左半段亮。
static void draw_star_bar(lv_layer_t *layer, int x, int y, int w, int half_stars) {
    const int seg_gap = 1;
    const int seg_w = (w - 2 * seg_gap) / 3;
    const int h = 3;
    if (seg_w <= 0) return;
    for (int i = 0; i < 3; i++) {
        const int half = half_stars - i * 2;
        const int sx = x + i * (seg_w + seg_gap);
        fill_rect(layer, sx, y, seg_w, h, 0x1B2A30u, 1, LV_OPA_70);
        if (half >= 2) {
            fill_rect(layer, sx, y, seg_w, h, CLR_STAR, 1, LV_OPA_COVER);
        } else if (half == 1) {
            fill_rect(layer, sx, y, (seg_w + 1) / 2, h, CLR_STAR, 1, LV_OPA_COVER);
        }
    }
}

static void draw_mmss(lv_layer_t *layer, int x, int y, int w, int h, uint32_t ms,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align) {
    const uint32_t total_s = ms / 1000u;
    const int mm = (int)(total_s / 60u);
    const int ss = (int)(total_s % 60u);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
    draw_text(layer, x, y, w, h, buf, font, color, align);
}

// ---------------------------------------------------------------------------
// 关卡列表
// ---------------------------------------------------------------------------
static int ui_highest_unlocked(void) {
    const catcube_ui_hooks_t *h = s_ui.hooks;
    int hi = h && h->highest_unlocked ? h->highest_unlocked() : 1;
    return hi < 1 ? 1 : hi;
}

static bool ui_is_cleared(int level) {
    return s_ui.hooks && s_ui.hooks->is_cleared ? s_ui.hooks->is_cleared(level) : false;
}

static int level_window_base(void) { return (s_ui.sel / LEVEL_PAGE_SIZE) * LEVEL_PAGE_SIZE; }

static void levels_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const int visible = ui_highest_unlocked(); // 只显示到"当前这一关"
    const lv_font_t *font = app_font_body();
    const int base = level_window_base();

    for (int i = 0; i < LEVEL_PAGE_SIZE; i++) {
        const int level = base + i + 1;
        if (level > visible) break; // 未通关的关不显示
        const int r = i / LEVEL_COLS;
        const int c = i % LEVEL_COLS;
        const int x = co.x1 + c * (LEVEL_CELL + LEVEL_GAP);
        const int y = co.y1 + r * (LEVEL_CELL + LEVEL_GAP);
        const bool cleared = ui_is_cleared(level);
        const uint32_t fill = cleared ? CLR_CLEARED : CLR_UNLOCKED;
        fill_rect(layer, x, y, LEVEL_CELL, LEVEL_CELL, fill, 3, LV_OPA_COVER);
        // 通关的关:数字上移一点,底部画三星条,星级一眼可见
        const int half = s_ui.hooks && s_ui.hooks->stars_get ? s_ui.hooks->stars_get(level) : 0;
        if (cleared && half > 0) {
            draw_number(layer, x, y - 3, LEVEL_CELL, LEVEL_CELL, level, font, 0xFFFFFFu,
                        LV_TEXT_ALIGN_CENTER);
            draw_star_bar(layer, x + 2, y + LEVEL_CELL - 6, LEVEL_CELL - 4, half);
        } else {
            draw_number(layer, x, y, LEVEL_CELL, LEVEL_CELL, level, font, 0xFFFFFFu,
                        LV_TEXT_ALIGN_CENTER);
        }
        if (level == s_ui.sel) {
            stroke_rect(layer, x - 1, y - 1, LEVEL_CELL + 2, LEVEL_CELL + 2, CLR_ACCENT, 2, 4);
        }
    }
}

// ---------------------------------------------------------------------------
// 游戏页:HUD(生命/计时)与棋盘
// ---------------------------------------------------------------------------
static void hud_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const cat_ui_t *ui = &s_ui;
    const lv_font_t *font = app_font_body();

    // 生命:两滴血,掉了的画暗
    for (int i = 0; i < CATCUBE_LIVES; i++) {
        const bool full = i < catcube_lives(&ui->st);
        draw_sprite(layer, co.x1 + i * 18, co.y1, 16, HEART_SPRITE,
                    full ? CLR_HEART : CLR_HEART_OFF, full ? 0xFF8A80u : CLR_HEART_OFF, 100, 0);
    }
    (void)font;
}

static void board_draw_cb(lv_event_t *e) {
    cat_ui_t *ui = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const catcube_level_def_t *lv = ui->st.level;
    if (!lv) return;

    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const int ox = co.x1;
    const int oy = co.y1;
    const int cell = ui->cell;

    // 1) 整块棋盘先铺一层深色缝底色:方块之间留 1~2px,看上去像一块块凸起的小砖。
    fill_rect(layer, ox, oy, lv->w * cell, lv->h * cell, 0x0A0F13u, 0, LV_OPA_COVER);
    const int inset = (cell >= 22) ? 2 : 1;
    for (int r = 0; r < lv->h; r++) {
        for (int c = 0; c < lv->w; c++) {
            const uint8_t region = catcube_region_at(&ui->st, (uint8_t)r, (uint8_t)c);
            const uint32_t base = REGION_COLORS[region % CATCUBE_MAX_SIDE];
            const int x = ox + c * cell + inset;
            const int y = oy + r * cell + inset;
            const int w = cell - 2 * inset;
            const int h = cell - 2 * inset;
            fill_rect(layer, x, y, w, h, base, 3, LV_OPA_COVER);
            // 顶部高光 + 底部阴影 = 轻微立体感(不是 3D,只是"凸起"的错觉)
            fill_rect(layer, x + 1, y + 1, w - 2, 2, mix_color(base, 0xFFFFFFu, 38), 2,
                      LV_OPA_60);
            fill_rect(layer, x + 1, y + h - 3, w - 2, 2, mix_color(base, 0x000000u, 38), 2,
                      LV_OPA_50);
        }
    }

    // 通关演出:放大 + 旋转
    int scale = 100;
    int quarter = 0;
    if (ui->win_phase == WIN_SCALE) {
        scale = 100 + (int)(50u * ui->win_ms / 500u);
        if (scale > 150) scale = 150;
    } else if (ui->win_phase == WIN_ROTATE) {
        scale = 150;
        quarter = (ui->win_ms / 150u) & 3;
    }

    for (int r = 0; r < lv->h; r++) {
        for (int c = 0; c < lv->w; c++) {
            const uint8_t idx = (uint8_t)(r * lv->w + c);
            const catcube_mark_t mark = catcube_mark_at(&ui->st, idx);
            int x = ox + c * cell;
            const int y = oy + r * cell;
            // 猜错那一格:左右抖一下(±2px,按 40ms 节拍)
            if (ui->shake_ms > 0 && (int)idx == ui->shake_idx) {
                x += ((ui->shake_ms / 40u) & 1u) ? 2 : -2;
            }
            if (mark == CATCUBE_MARK_WRONG) {
                draw_sprite(layer, x, y, cell, X_SPRITE, CLR_DANGER, CLR_DANGER, 100, 0);
            } else if (mark == CATCUBE_MARK_X) {
                const uint32_t x_color = catcube_mark_is_auto(&ui->st, idx) ? CLR_X_AUTO : CLR_X;
                draw_sprite(layer, x, y, cell, X_SPRITE, x_color, x_color, 100, 0);
            } else if (mark == CATCUBE_MARK_CAT) {
                draw_sprite(layer, x, y, cell, CAT_SPRITE, CLR_CAT, CLR_CAT_EYE, scale, quarter);
                if (catcube_cell_conflict(&ui->st, idx) != CATCUBE_CONFLICT_NONE) {
                    stroke_rect(layer, x + 1, y + 1, cell - 2, cell - 2, CLR_DANGER, 2, 4);
                }
            }
        }
    }

    stroke_rect(layer, ox, oy, lv->w * cell, lv->h * cell, 0x1B1B1Bu, 2, 4);

    if (ui->win_phase == WIN_IDLE || ui->win_phase == WIN_DONE) {
        uint8_t cr = 0;
        uint8_t cc = 0;
        catcube_cursor_rc(&ui->st, &cr, &cc);
        const int cx = ox + cc * cell;
        const int cy = oy + cr * cell;
        fill_rect(layer, cx - 3, cy - 3, cell + 6, cell + 6, CLR_ACCENT, 5, LV_OPA_30);
        stroke_rect(layer, cx - 2, cy - 2, cell + 4, cell + 4, CLR_ACCENT, 3, 3);
    }
}

// ---------------------------------------------------------------------------
// 音量页:一条进度条 + 数值
// ---------------------------------------------------------------------------
static void volume_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const lv_font_t *title_font = app_font_title();
    const lv_font_t *body = app_font_body();
    const int volume = s_ui.hooks && s_ui.hooks->volume_get ? s_ui.hooks->volume_get() : 0;
    const bool muted = s_ui.hooks && s_ui.hooks->muted_get ? s_ui.hooks->muted_get() : false;

    fill_rect(layer, co.x1, co.y1, co.x2 - co.x1 + 1, co.y2 - co.y1 + 1, 0x000000u, 0, LV_OPA_70);
    fill_rect(layer, co.x1 + 10, co.y1 + 70, 220, 160, CLR_CARD, 12, LV_OPA_COVER);
    stroke_rect(layer, co.x1 + 10, co.y1 + 70, 220, 160, 0x0A1014, 2, 12);

    draw_text(layer, co.x1 + 10, co.y1 + 78, 220, 30, CAT_VOL_TITLE, title_font, CLR_TEXT,
              LV_TEXT_ALIGN_CENTER);

    // 进度条
    const int bar_x = co.x1 + 30;
    const int bar_y = co.y1 + 124;
    const int bar_w = 180;
    const int bar_h = 22;
    fill_rect(layer, bar_x, bar_y, bar_w, bar_h, 0x101820u, 6, LV_OPA_COVER);
    const int fill_w = bar_w * (volume < 0 ? 0 : (volume > 100 ? 100 : volume)) / 100;
    if (fill_w > 0) {
        fill_rect(layer, bar_x, bar_y, fill_w, bar_h, muted ? CLR_TEXT_DIM : CLR_ACCENT, 6,
                  LV_OPA_COVER);
    }
    stroke_rect(layer, bar_x, bar_y, bar_w, bar_h, 0x37474Fu, 1, 6);
    draw_number(layer, bar_x, bar_y + bar_h + 4, bar_w, 20, volume, body, CLR_TEXT,
                LV_TEXT_ALIGN_CENTER);
    if (muted) {
        draw_text(layer, co.x1 + 10, co.y1 + 170, 220, 20, CAT_MENU_MUTE_ON, body, CLR_DANGER,
                  LV_TEXT_ALIGN_CENTER);
    }
    draw_text(layer, co.x1 + 10, co.y1 + 196, 220, 20, CAT_VOL_HINT, body, CLR_TEXT_DIM,
              LV_TEXT_ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// 新手教学(前 5 关):小示意图 + 一句话规则
// ---------------------------------------------------------------------------
// 4x4 迷你棋盘:区域图 / 猫 / 叉 各 16 格。用同一套像素素材,和真实棋盘观感一致。
static void draw_mini_board(lv_layer_t *layer, int x, int y, const uint8_t *regions,
                            const uint8_t *cats, const uint8_t *marks) {
    const int cell = 12;
    fill_rect(layer, x, y, 4 * cell, 4 * cell, 0x0A0F13u, 0, LV_OPA_COVER);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            const uint32_t base = REGION_COLORS[regions[r * 4 + c] % CATCUBE_MAX_SIDE];
            const int tx = x + c * cell + 1;
            const int ty = y + r * cell + 1;
            fill_rect(layer, tx, ty, cell - 2, cell - 2, base, 2, LV_OPA_COVER);
            fill_rect(layer, tx + 1, ty + 1, cell - 4, 1, mix_color(base, 0xFFFFFFu, 38), 1,
                      LV_OPA_60);
            if (cats[r * 4 + c]) {
                draw_sprite(layer, x + c * cell, y + r * cell, cell, CAT_SPRITE, CLR_CAT,
                            CLR_CAT_EYE, 100, 0);
            } else if (marks[r * 4 + c]) {
                draw_sprite(layer, x + c * cell, y + r * cell, cell, X_SPRITE, CLR_X, CLR_X, 100,
                            0);
            }
        }
    }
}

static void tutorial_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const lv_font_t *title_font = app_font_title();
    const lv_font_t *body = app_font_body();

    fill_rect(layer, co.x1, co.y1, co.x2 - co.x1 + 1, co.y2 - co.y1 + 1, 0x000000u, 0, LV_OPA_70);
    fill_rect(layer, co.x1 + 10, co.y1 + 30, 220, 260, CLR_CARD, 12, LV_OPA_COVER);
    stroke_rect(layer, co.x1 + 10, co.y1 + 30, 220, 260, 0x0A1014, 2, 12);

    draw_text(layer, co.x1 + 10, co.y1 + 36, 220, 30, CAT_TUT_TITLE, title_font, CLR_TEXT,
              LV_TEXT_ALIGN_CENTER);

    // 图 1:同一行/同一列只能一只
    static const uint8_t one_region[16] = {0};
    uint8_t cats1[16] = {0};
    uint8_t marks1[16] = {0};
    cats1[1 * 4 + 1] = 1;
    for (int c = 0; c < 4; c++) marks1[1 * 4 + c] = (c != 1);
    for (int r = 0; r < 4; r++) marks1[r * 4 + 1] = (r != 1);
    draw_mini_board(layer, co.x1 + 22, co.y1 + 74, one_region, cats1, marks1);
    draw_text(layer, co.x1 + 80, co.y1 + 74, 140, 48, CAT_TUT_1, body, CLR_TEXT,
              LV_TEXT_ALIGN_LEFT);

    // 图 2:同一色块只能一只(左半块一个颜色)
    static const uint8_t two_regions[16] = {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1};
    uint8_t cats2[16] = {0};
    uint8_t marks2[16] = {0};
    cats2[1 * 4 + 0] = 1;
    // 同色块(左半 4x2)除猫以外全部排除 —— 这才是真实规则
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 2; c++) {
            if (r == 1 && c == 0) continue;
            marks2[r * 4 + c] = 1;
        }
    }
    draw_mini_board(layer, co.x1 + 22, co.y1 + 130, two_regions, cats2, marks2);
    draw_text(layer, co.x1 + 80, co.y1 + 130, 140, 48, CAT_TUT_2, body, CLR_TEXT,
              LV_TEXT_ALIGN_LEFT);

    // 图 3:八邻域(含斜角)都不能有猫
    uint8_t cats3[16] = {0};
    uint8_t marks3[16] = {0};
    cats3[1 * 4 + 1] = 1;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (!dr && !dc) continue;
            const int r = 1 + dr;
            const int c = 1 + dc;
            if (r >= 0 && r < 4 && c >= 0 && c < 4) marks3[r * 4 + c] = 1;
        }
    }
    draw_mini_board(layer, co.x1 + 22, co.y1 + 186, one_region, cats3, marks3);
    draw_text(layer, co.x1 + 80, co.y1 + 186, 140, 48, CAT_TUT_3, body, CLR_TEXT,
              LV_TEXT_ALIGN_LEFT);

    draw_text(layer, co.x1 + 20, co.y1 + 240, 200, 20, CAT_TUT_4, body, CLR_ACCENT,
              LV_TEXT_ALIGN_CENTER);
    draw_text(layer, co.x1 + 20, co.y1 + 262, 200, 20,
              s_ui.tutorial_from_menu ? CAT_TUT_BACK : CAT_TUT_START, body, CLR_TEXT_DIM,
              LV_TEXT_ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// 通关演出 + 结算页(自绘)
// ---------------------------------------------------------------------------
static void win_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const cat_ui_t *ui = &s_ui;
    const lv_font_t *title_font = app_font_title();
    const lv_font_t *body = app_font_body();

    fill_rect(layer, co.x1, co.y1, co.x2 - co.x1 + 1, co.y2 - co.y1 + 1, 0x000000u, 0, LV_OPA_70);

    draw_text(layer, co.x1, co.y1 + 24, co.x2 - co.x1 + 1, 30, CAT_WIN_TITLE, title_font,
              CLR_ACCENT, LV_TEXT_ALIGN_CENTER);

    // 进度条:每只猫一个格子,一只一只填进去
    const int k = catcube_target(&ui->st);
    if (k > 0) {
        const int slot = 20;
        const int gap = 2;
        const int total_w = k * slot + (k - 1) * gap;
        const int bx = co.x1 + (co.x2 - co.x1 + 1 - total_w) / 2;
        const int by = co.y1 + 74;
        for (int i = 0; i < k; i++) {
            const int x = bx + i * (slot + gap);
            fill_rect(layer, x, by, slot, slot, 0x1B2429u, 4, LV_OPA_COVER);
            if (i < ui->win_bar) {
                draw_sprite(layer, x, by, slot, CAT_SPRITE, CLR_CAT, CLR_CAT_EYE, 100, 0);
            } else {
                stroke_rect(layer, x, by, slot, slot, 0x37474Fu, 1, 4);
            }
        }
    }

    if (ui->win_phase == WIN_DONE) {
        // 星级:6 个半星 = 3 星
        const int star_cell = 26;
        const int gap = 4;
        const int total_w = 3 * star_cell + 2 * gap;
        const int sx = co.x1 + (co.x2 - co.x1 + 1 - total_w) / 2;
        const int sy = co.y1 + 108;
        for (int i = 0; i < 3; i++) {
            const int half = ui->win_stars - i * 2;
            draw_star(layer, sx + i * (star_cell + gap), sy, star_cell,
                      half >= 2 ? 2 : (half == 1 ? 1 : 0));
        }
        draw_mmss(layer, co.x1, co.y1 + 148, co.x2 - co.x1 + 1, 18, ui->final_ms, body, CLR_TEXT,
                  LV_TEXT_ALIGN_CENTER);
    }
}

static void fail_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const lv_font_t *title_font = app_font_title();
    fill_rect(layer, co.x1, co.y1, co.x2 - co.x1 + 1, co.y2 - co.y1 + 1, 0x000000u, 0, LV_OPA_70);
    draw_text(layer, co.x1, co.y1 + 70, co.x2 - co.x1 + 1, 30, CAT_FAIL_TITLE, title_font,
              CLR_DANGER, LV_TEXT_ALIGN_CENTER);
    draw_text(layer, co.x1, co.y1 + 110, co.x2 - co.x1 + 1, 20,
              s_ui.limit_ms > 0 ? CAT_FAIL_TIME : CAT_FAIL_LIVES, app_font_body(), CLR_TEXT_DIM,
              LV_TEXT_ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// 页面构建
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y,
                            int w, int h, lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *make_canvas_obj(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *text, int width, int height) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, width);
    lv_obj_set_height(row, height);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_style_text_font(label, app_font_body(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(label, width, height);
    lv_obj_set_pos(label, 0, 0);
    lv_label_set_text(label, text);
    lv_obj_set_style_pad_top(label, (height - (int)app_font_body()->line_height) / 2, 0);
    return row;
}

static void set_row_active(lv_obj_t *row, bool active) {
    lv_obj_set_style_bg_opa(row, active ? LV_OPA_30 : LV_OPA_TRANSP, 0);
}

static lv_obj_t *make_card(lv_obj_t *parent, int height) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, height);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0A1014), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(card);
    return card;
}

static void build_levels_page(void) {
    cat_ui_t *ui = &s_ui;
    ui->lv_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ui->lv_page);
    lv_obj_set_size(ui->lv_page, UI_W, UI_H);
    lv_obj_clear_flag(ui->lv_page, LV_OBJ_FLAG_SCROLLABLE);

    ui->lv_title = make_label(ui->lv_page, app_font_title(), CLR_TEXT, 10, 4, 140, 30,
                              LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(ui->lv_title, CAT_SELECT_TITLE);
    ui->lv_battery = make_label(ui->lv_page, app_font_body(), CLR_TEXT_DIM, 130, 10, 100, 18,
                                LV_TEXT_ALIGN_RIGHT);
    ui->lv_progress = make_label(ui->lv_page, app_font_body(), CLR_TEXT_DIM, 10, 34, 130, 18,
                                 LV_TEXT_ALIGN_LEFT);
    ui->lv_page_range = make_label(ui->lv_page, app_font_body(), CLR_TEXT_DIM, 140, 34, 90, 18,
                                   LV_TEXT_ALIGN_RIGHT);

    ui->lv_grid = make_canvas_obj(ui->lv_page, LEVEL_GRID_X, LEVEL_GRID_Y,
                                  LEVEL_COLS * LEVEL_CELL + (LEVEL_COLS - 1) * LEVEL_GAP,
                                  (LEVEL_PAGE_SIZE / LEVEL_COLS) * (LEVEL_CELL + LEVEL_GAP) -
                                      LEVEL_GAP);
    lv_obj_add_event_cb(ui->lv_grid, levels_draw_cb, LV_EVENT_DRAW_MAIN, ui);
}

static void build_play_page(void) {
    cat_ui_t *ui = &s_ui;
    ui->pl_page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ui->pl_page);
    lv_obj_set_size(ui->pl_page, UI_W, UI_H);
    lv_obj_clear_flag(ui->pl_page, LV_OBJ_FLAG_SCROLLABLE);

    // 第一行:关卡号(左) + 生命(中) + 电量(右上角)
    ui->pl_title = make_label(ui->pl_page, app_font_title(), CLR_TEXT, 8, 2, 100, 28,
                              LV_TEXT_ALIGN_LEFT);
    ui->pl_hud = make_canvas_obj(ui->pl_page, 110, 6, 60, 18);
    lv_obj_add_event_cb(ui->pl_hud, hud_draw_cb, LV_EVENT_DRAW_MAIN, ui);
    ui->pl_battery = make_label(ui->pl_page, app_font_body(), CLR_TEXT_DIM, 172, 4, 60, 18,
                                LV_TEXT_ALIGN_RIGHT);

    // 第二行:尺寸 / 找到数 / 计时
    ui->pl_size = make_label(ui->pl_page, app_font_body(), CLR_TEXT_DIM, 8, 30, 52, 18,
                             LV_TEXT_ALIGN_LEFT);
    ui->pl_cats = make_label(ui->pl_page, app_font_body(), CLR_ACCENT, 62, 30, 92, 18,
                             LV_TEXT_ALIGN_LEFT);
    ui->pl_timer = make_label(ui->pl_page, app_font_body(), CLR_TEXT_DIM, 156, 30, 76, 18,
                              LV_TEXT_ALIGN_RIGHT);

    ui->pl_board = make_canvas_obj(ui->pl_page, PLAY_BOARD_X, PLAY_BOARD_Y, PLAY_BOARD_MAX,
                                   PLAY_BOARD_MAX);
    lv_obj_add_event_cb(ui->pl_board, board_draw_cb, LV_EVENT_DRAW_MAIN, ui);

    ui->pl_hint = make_label(ui->pl_page, app_font_body(), CLR_TEXT, 8, 284, 224, 32,
                             LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(ui->pl_hint, LV_LABEL_LONG_WRAP);
}

static void build_overlays(void) {
    cat_ui_t *ui = &s_ui;

    ui->ov_bg = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ui->ov_bg);
    lv_obj_set_size(ui->ov_bg, UI_W, UI_H);
    lv_obj_set_style_bg_color(ui->ov_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui->ov_bg, LV_OPA_60, 0);
    lv_obj_clear_flag(ui->ov_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->ov_bg, LV_OBJ_FLAG_HIDDEN);

    ui->card_menu = make_card(ui->ov_bg, 8 + 30 + MENU_ITEMS * 30 + 4 * (MENU_ITEMS - 1) + 8);
    lv_obj_t *menu_title = make_label(ui->card_menu, app_font_title(), CLR_TEXT, 0, 0, CARD_W - 24,
                                      30, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(menu_title, CAT_MENU_TITLE);
    for (int i = 0; i < MENU_ITEMS; i++) {
        ui->menu_row[i] = make_row(ui->card_menu, "", CARD_W - 24, 30);
        ui->menu_label[i] = lv_obj_get_child(ui->menu_row[i], 0);
    }

    // 通关演出 + 结算
    ui->win_page = make_canvas_obj(lv_screen_active(), 0, 0, UI_W, UI_H);
    lv_obj_add_event_cb(ui->win_page, win_draw_cb, LV_EVENT_DRAW_MAIN, ui);
    lv_obj_add_flag(ui->win_page, LV_OBJ_FLAG_HIDDEN);
    ui->win_row[0] = make_row(ui->win_page, CAT_WIN_NEXT, 180, 34);
    lv_obj_set_pos(ui->win_row[0], 30, 236);
    ui->win_row[1] = make_row(ui->win_page, CAT_WIN_RETRY, 180, 34);
    lv_obj_set_pos(ui->win_row[1], 30, 278);
    lv_obj_add_flag(ui->win_row[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->win_row[1], LV_OBJ_FLAG_HIDDEN);

    // 新手教学
    ui->tut_page = make_canvas_obj(lv_screen_active(), 0, 0, UI_W, UI_H);
    lv_obj_add_event_cb(ui->tut_page, tutorial_draw_cb, LV_EVENT_DRAW_MAIN, ui);
    lv_obj_add_flag(ui->tut_page, LV_OBJ_FLAG_HIDDEN);

    // 音量
    ui->vol_page = make_canvas_obj(lv_screen_active(), 0, 0, UI_W, UI_H);
    lv_obj_add_event_cb(ui->vol_page, volume_draw_cb, LV_EVENT_DRAW_MAIN, ui);
    lv_obj_add_flag(ui->vol_page, LV_OBJ_FLAG_HIDDEN);

    // 失败
    ui->fail_page = make_canvas_obj(lv_screen_active(), 0, 0, UI_W, UI_H);
    lv_obj_add_event_cb(ui->fail_page, fail_draw_cb, LV_EVENT_DRAW_MAIN, ui);
    lv_obj_add_flag(ui->fail_page, LV_OBJ_FLAG_HIDDEN);
    ui->fail_row[0] = make_row(ui->fail_page, CAT_WIN_RETRY, 180, 34);
    lv_obj_set_pos(ui->fail_row[0], 30, 236);
    ui->fail_row[1] = make_row(ui->fail_page, CAT_WIN_LIST, 180, 34);
    lv_obj_set_pos(ui->fail_row[1], 30, 278);
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------
static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_battery(lv_obj_t *label) {
    if (!label) return;
    const int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) {
        snprintf(buf, sizeof(buf), "%s", CAT_BATTERY_NA);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", soc);
    }
    lv_label_set_text(label, buf);
}

static void refresh_levels(void) {
    cat_ui_t *ui = &s_ui;
    const int visible = ui_highest_unlocked();
    const int cleared = visible - 1;
    const int base = level_window_base();
    char buf[64];
    snprintf(buf, sizeof(buf), CAT_PROGRESS_FMT, cleared);
    lv_label_set_text(ui->lv_progress, buf);
    snprintf(buf, sizeof(buf), "%d - %d", base + 1, base + LEVEL_PAGE_SIZE);
    lv_label_set_text(ui->lv_page_range, buf);
    refresh_battery(ui->lv_battery);
    lv_obj_invalidate(ui->lv_grid);
}

static void refresh_play(void) {
    cat_ui_t *ui = &s_ui;
    const catcube_level_def_t *lv = ui->st.level;
    if (!lv) return;

    char buf[48];
    snprintf(buf, sizeof(buf), CAT_LEVEL_FMT, ui->level);
    lv_label_set_text(ui->pl_title, buf);
    snprintf(buf, sizeof(buf), CAT_SIZE_FMT, lv->w, lv->h);
    lv_label_set_text(ui->pl_size, buf);
    snprintf(buf, sizeof(buf), CAT_CATS_FMT, catcube_found(&ui->st), (int)lv->k);
    lv_label_set_text(ui->pl_cats, buf);
    lv_label_set_text(ui->pl_hint, CAT_HINT_MOVE "\n" CAT_HINT_MARK);
    refresh_battery(ui->pl_battery);
    lv_obj_invalidate(ui->pl_board);
    lv_obj_invalidate(ui->pl_hud);
}

// 两级菜单:主菜单 6 项,设置页 5 项。行对象复用,按页改文案与显隐。
static void menu_set_page(int page) {
    cat_ui_t *ui = &s_ui;
    static const char *const main_texts[MENU_MAIN_ITEMS] = {
        CAT_MENU_RESUME, CAT_MENU_RESTART, CAT_MENU_HINT, CAT_MENU_SETTINGS, CAT_MENU_RULES,
        CAT_MENU_LIST,
    };
    static const char *const set_texts[MENU_SET_ITEMS] = {
        CAT_MENU_AUTO_ON, CAT_MENU_VOLUME, CAT_MENU_MUTE_OFF, CAT_MENU_STANDBY_OFF,
        CAT_MENU_RESET,   CAT_MENU_BACK,
    };
    ui->menu_page = page;
    ui->menu_sel = 0;
    ui->reset_armed = false;
    const int count = (page == 0) ? MENU_MAIN_ITEMS : MENU_SET_ITEMS;
    for (int i = 0; i < MENU_ITEMS; i++) {
        set_hidden(ui->menu_row[i], i >= count);
        if (i < count && ui->menu_label[i]) {
            lv_label_set_text(ui->menu_label[i], page == 0 ? main_texts[i] : set_texts[i]);
        }
    }
}

static void overlay_show(cat_overlay_t which) {
    cat_ui_t *ui = &s_ui;
    ui->overlay = which;
    set_hidden(ui->ov_bg, which != OV_MENU);
    set_hidden(ui->card_menu, which != OV_MENU);
    set_hidden(ui->win_page, which != OV_WIN);
    set_hidden(ui->fail_page, which != OV_FAIL);
    set_hidden(ui->tut_page, which != OV_TUTORIAL);
    set_hidden(ui->vol_page, which != OV_VOLUME);

    if (which == OV_MENU) {
        if (ui->menu_page == 1) {
            if (ui->menu_label[0]) {
                lv_label_set_text(ui->menu_label[0], catcube_auto_exclude(&ui->st)
                                                         ? CAT_MENU_AUTO_ON
                                                         : CAT_MENU_AUTO_OFF);
            }
            if (ui->menu_label[2]) {
                const bool muted = ui->hooks && ui->hooks->muted_get ? ui->hooks->muted_get() : false;
                lv_label_set_text(ui->menu_label[2], muted ? CAT_MENU_MUTE_ON : CAT_MENU_MUTE_OFF);
            }
            if (ui->menu_label[3]) {
                const int minutes =
                    ui->hooks && ui->hooks->standby_get ? ui->hooks->standby_get() : 0;
                if (minutes > 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), CAT_MENU_STANDBY_FMT, minutes);
                    lv_label_set_text(ui->menu_label[3], buf);
                } else {
                    lv_label_set_text(ui->menu_label[3], CAT_MENU_STANDBY_OFF);
                }
            }
            if (ui->menu_label[4]) {
                lv_label_set_text(ui->menu_label[4],
                                  ui->reset_armed ? CAT_MENU_RESET_ARM : CAT_MENU_RESET);
            }
        }
        for (int i = 0; i < MENU_ITEMS; i++) set_row_active(ui->menu_row[i], i == ui->menu_sel);
    }
    if (which == OV_WIN) {
        for (int i = 0; i < WIN_ITEMS; i++) set_row_active(ui->win_row[i], i == ui->win_sel);
    }
    if (which == OV_FAIL) {
        for (int i = 0; i < WIN_ITEMS; i++) set_row_active(ui->fail_row[i], i == ui->win_sel);
    }
}

// ---------------------------------------------------------------------------
// 关卡进入 / 通关 / 失败
// ---------------------------------------------------------------------------
void catcube_ui_show_levels(void) {
    cat_ui_t *ui = &s_ui;
    ui->page = PAGE_LEVELS;
    ui->overlay = OV_NONE;
    ui->win_phase = WIN_IDLE;
    if (!ui->sel_initialized) {
        ui->sel = ui_highest_unlocked();
        ui->sel_initialized = true;
    }
    if (ui->sel < 1) ui->sel = 1;
    if (ui->sel > ui_highest_unlocked()) ui->sel = ui_highest_unlocked();
    set_hidden(ui->lv_page, false);
    set_hidden(ui->pl_page, true);
    set_hidden(ui->ov_bg, true);
    set_hidden(ui->win_page, true);
    set_hidden(ui->fail_page, true);
    refresh_levels();
}

void catcube_ui_show_play(int level) {
    cat_ui_t *ui = &s_ui;
    if (level < 1) return;
    const catcube_level_def_t *lv = catcube_level_at(level);
    if (!lv) return;

    ui->level = level;
    ui->level_serial++;
    ui->page = PAGE_PLAY;
    ui->overlay = OV_NONE;
    ui->menu_sel = 0;
    ui->win_sel = 0;
    ui->win_phase = WIN_IDLE;
    ui->win_ms = 0;
    ui->win_bar = 0;
    ui->win_stars = 0;
    ui->clock_ms = 0;
    ui->limit_ms = catcube_time_limit_ms(level);

    catcube_state_reset(&ui->st, lv);

    int side = PLAY_BOARD_MAX / (lv->w > lv->h ? lv->w : lv->h);
    if (side > PLAY_CELL_MAX) side = PLAY_CELL_MAX;
    if (side < 8) side = 8;
    ui->cell = side;
    ui->board_side = side * (lv->w > lv->h ? lv->w : lv->h);

    const int bx = PLAY_BOARD_X + (PLAY_BOARD_MAX - ui->board_side) / 2;
    const int by = PLAY_BOARD_Y + (PLAY_BOARD_MAX - ui->board_side) / 2;
    lv_obj_set_pos(ui->pl_board, bx, by);
    lv_obj_set_size(ui->pl_board, side * lv->w, side * lv->h);

    set_hidden(ui->lv_page, true);
    set_hidden(ui->pl_page, false);
    set_hidden(ui->ov_bg, true);
    set_hidden(ui->win_page, true);
    set_hidden(ui->fail_page, true);
    refresh_play();

    // 前 5 关每次进入都先看一遍带图的规则;第 6 关起不再打扰。
    if (level <= 5) {
        ui->tutorial_from_menu = false;
        overlay_show(OV_TUTORIAL);
    }
}

static void start_win(void) {
    cat_ui_t *ui = &s_ui;
    ui->win_phase = WIN_SCALE;
    ui->win_ms = 0;
    ui->win_bar = 0;
    ui->final_ms = ui->clock_ms;
    ui->win_stars = catcube_stars_half(&ui->st, ui->final_ms);
    // 星级随通关一起落盘:选关界面按最好成绩显示
    if (ui->hooks && ui->hooks->mark_cleared) {
        ui->hooks->mark_cleared(ui->level, ui->win_stars);
    }
    lv_obj_invalidate(ui->pl_board);
}

static void show_fail(void) {
    cat_ui_t *ui = &s_ui;
    if (ui->overlay == OV_FAIL) return;
    ui->win_sel = 0;
    overlay_show(OV_FAIL);
}

int catcube_ui_current_level(void) { return s_ui.page == PAGE_PLAY ? s_ui.level : 0; }
uint32_t catcube_ui_level_serial(void) { return s_ui.level_serial; }

bool catcube_ui_clock_running(void) {
    // 打开菜单/设置/音量/玩法/教学都暂停,限时关的倒计时也一起停。
    return s_ui.page == PAGE_PLAY && s_ui.overlay == OV_NONE &&
           s_ui.win_phase == WIN_IDLE && !catcube_failed(&s_ui.st);
}

bool catcube_ui_anim_active(void) {
    const win_phase_t p = s_ui.win_phase;
    if (s_ui.shake_ms > 0 || s_ui.sweep_active || s_ui.toast_ms > 0) return true;
    return p == WIN_SCALE || p == WIN_ROTATE || p == WIN_BAR;
}

void catcube_ui_tick_anim(uint32_t dt_ms) {
    cat_ui_t *ui = &s_ui;

    // 红叉抖动:独立于通关演出
    if (ui->shake_ms > 0) {
        ui->shake_ms = (ui->shake_ms > dt_ms) ? ui->shake_ms - dt_ms : 0;
        lv_obj_invalidate(ui->pl_board);
    }
    if (ui->sweep_active) {
        ui->sweep_ms += dt_ms;
        if (ui->sweep_ms > 600u) ui->sweep_active = false;
        lv_obj_invalidate(ui->pl_board);
    }
    if (ui->toast_ms > 0) {
        ui->toast_ms = (ui->toast_ms > dt_ms) ? ui->toast_ms - dt_ms : 0;
        if (ui->toast_ms == 0) {
            lv_label_set_text(ui->pl_hint, CAT_HINT_MOVE "\n" CAT_HINT_MARK);
            lv_obj_set_style_text_color(ui->pl_hint, lv_color_hex(CLR_TEXT), 0);
        }
    }

    if (ui->win_phase == WIN_IDLE || ui->win_phase == WIN_DONE) return;

    ui->win_ms += dt_ms;
    switch (ui->win_phase) {
        case WIN_SCALE:
            if (ui->win_ms >= 500) {
                ui->win_phase = WIN_ROTATE;
                ui->win_ms = 0;
            }
            lv_obj_invalidate(ui->pl_board);
            break;
        case WIN_ROTATE:
            if (ui->win_ms >= 600) {
                ui->win_phase = WIN_BAR;
                ui->win_ms = 0;
                ui->win_bar = 0;
                overlay_show(OV_WIN);
            }
            lv_obj_invalidate(ui->pl_board);
            break;
        case WIN_BAR: {
            const int k = catcube_target(&ui->st);
            const uint32_t per_slot = 110;
            const int want = (int)(ui->win_ms / per_slot);
            if (want > ui->win_bar && ui->win_bar < k) {
                ui->win_bar = want > k ? k : want;
                cat_audio_play(CAT_SND_SLOT, 0);
                lv_obj_invalidate(ui->win_page);
            }
            if (ui->win_bar >= k && ui->win_ms >= per_slot * (uint32_t)k + 120) {
                ui->win_phase = WIN_DONE;
                cat_audio_play(CAT_SND_WIN, 0);
                set_hidden(ui->win_row[0], false);
                set_hidden(ui->win_row[1], false);
                lv_obj_invalidate(ui->win_page);
            }
            break;
        }
        default:
            break;
    }
}

void catcube_ui_set_clock(uint32_t elapsed_ms, uint32_t limit_ms) {
    cat_ui_t *ui = &s_ui;
    if (ui->win_phase != WIN_IDLE || catcube_failed(&ui->st)) return; // 已经结束,冻结计时
    ui->clock_ms = elapsed_ms;
    ui->limit_ms = limit_ms;
    // 限时关显示剩余时间,最后 30s 变红
    char buf[16];
    if (limit_ms > 0) {
        const uint32_t left = (elapsed_ms >= limit_ms) ? 0 : (limit_ms - elapsed_ms);
        snprintf(buf, sizeof(buf), "%d:%02d", (int)(left / 60000u), (int)(left / 1000u % 60u));
        lv_obj_set_style_text_color(ui->pl_timer, lv_color_hex(left < 30000u ? CLR_DANGER : CLR_TEXT), 0);
    } else {
        snprintf(buf, sizeof(buf), "%d:%02d", (int)(elapsed_ms / 60000u),
                 (int)(elapsed_ms / 1000u % 60u));
        lv_obj_set_style_text_color(ui->pl_timer, lv_color_hex(CLR_TEXT_DIM), 0);
    }
    lv_label_set_text(ui->pl_timer, buf);
    if (limit_ms > 0 && elapsed_ms >= limit_ms && ui->page == PAGE_PLAY &&
        ui->win_phase == WIN_IDLE) {
        catcube_mark_failed(&ui->st);
        show_fail();
    }
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
static void key_levels(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    const int visible = ui_highest_unlocked();
    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        const int step = (ev == BSP_BTN_CLICK) ? 1 : -1;
        if (btn == BSP_BTN_UP) {
            if (step > 0) {
                if (ui->sel % LEVEL_COLS == 0 && ui->sel > 1) ui->sel += LEVEL_COLS - 1;
                else ui->sel += 1;
            } else {
                if (ui->sel % LEVEL_COLS == 1 || ui->sel == 1) ui->sel -= LEVEL_COLS - 1;
                else ui->sel -= 1;
            }
            if (ui->sel < 1) ui->sel = visible;
            if (ui->sel > visible) ui->sel = 1;
        } else {
            ui->sel += LEVEL_COLS * step;
            if (ui->sel < 1) ui->sel = 1 + (ui->sel - 1 + LEVEL_PAGE_SIZE) % LEVEL_COLS;
            if (ui->sel > visible) ui->sel = 1 + (ui->sel - 1) % LEVEL_COLS;
        }
        refresh_levels();
    } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        catcube_ui_show_play(ui->sel);
    } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        ui->sel = visible;
        refresh_levels();
    }
}

static void key_menu(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    const int count = (ui->menu_page == 0) ? MENU_MAIN_ITEMS : MENU_SET_ITEMS;

    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        // 单击:上=上一项/下=下一项;双击反向
        const int step = (ev == BSP_BTN_CLICK) ? 1 : -1;
        const int dir = (btn == BSP_BTN_UP) ? -1 : 1;
        ui->menu_sel = ((ui->menu_sel + dir * step) % count + count) % count;
        ui->reset_armed = false;
        overlay_show(OV_MENU);
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        overlay_show(OV_NONE);
        return;
    }
    if (ev != BSP_BTN_CLICK || btn != BSP_BTN_OK) return;

    if (ui->menu_page == 0) {
        switch (ui->menu_sel) {
            case 1: // 重开本关
                catcube_ui_show_play(ui->level);
                return;
            case 2: // 提示
                overlay_show(OV_NONE);
                if (catcube_hint(&ui->st)) {
                    cat_audio_play(CAT_SND_CAT, 0);
                    ui->sweep_ms = 0;
                    ui->sweep_r = (int)(catcube_cursor(&ui->st) / ui->st.level->w);
                    ui->sweep_c = (int)(catcube_cursor(&ui->st) % ui->st.level->w);
                }
                refresh_play();
                if (catcube_is_solved(&ui->st)) start_win();
                return;
            case 3: // 设置
                menu_set_page(1);
                overlay_show(OV_MENU);
                return;
            case 4: // 玩法说明:就是那张带示意图的弹窗
                ui->tutorial_from_menu = true;
                overlay_show(OV_TUTORIAL);
                return;
            case 5: // 关卡列表
                catcube_ui_show_levels();
                return;
            default: // 继续
                overlay_show(OV_NONE);
                return;
        }
    }

    switch (ui->menu_sel) {
        case 0: // 自动排除
            catcube_set_auto_exclude(&ui->st, !catcube_auto_exclude(&ui->st));
            overlay_show(OV_MENU);
            refresh_play();
            return;
        case 1: // 音量
            overlay_show(OV_VOLUME);
            return;
        case 2: // 静音
            if (ui->hooks && ui->hooks->muted_set && ui->hooks->muted_get) {
                ui->hooks->muted_set(!ui->hooks->muted_get());
            }
            overlay_show(OV_MENU);
            return;
        case 3: { // 待机时长:关闭 / 1 / 3 / 5 / 10 分钟 循环
            static const int kOptions[] = {0, 1, 3, 5, 10};
            const int cur = ui->hooks && ui->hooks->standby_get ? ui->hooks->standby_get() : 0;
            int next = kOptions[0];
            for (unsigned i = 0; i < sizeof(kOptions) / sizeof(kOptions[0]); i++) {
                if (kOptions[i] == cur) {
                    next = kOptions[(i + 1) % (sizeof(kOptions) / sizeof(kOptions[0]))];
                    break;
                }
            }
            if (ui->hooks && ui->hooks->standby_set) ui->hooks->standby_set(next);
            overlay_show(OV_MENU);
            return;
        }
        case 4: // 重置进度(二次确认)
            if (!ui->reset_armed) {
                ui->reset_armed = true;
                overlay_show(OV_MENU);
                return;
            }
            ui->reset_armed = false;
            if (ui->hooks && ui->hooks->reset_progress) ui->hooks->reset_progress();
            ui->sel = 1;
            ui->sel_initialized = true;
            catcube_ui_show_levels();
            return;
        default: // 返回主菜单
            menu_set_page(0);
            overlay_show(OV_MENU);
            return;
    }
}

static void key_win(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    if (ui->win_phase != WIN_DONE) return; // 演出期间不接受输入
    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        ui->win_sel = (ui->win_sel + 1) % WIN_ITEMS;
        overlay_show(OV_WIN);
    } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        if (ui->win_sel == 0) catcube_ui_show_play(ui->level + 1);
        else catcube_ui_show_play(ui->level);
    }
}

static void key_fail(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        ui->win_sel = (ui->win_sel + 1) % WIN_ITEMS;
        overlay_show(OV_FAIL);
    } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        if (ui->win_sel == 0) catcube_ui_show_play(ui->level);
        else catcube_ui_show_levels();
    }
}

static void key_play(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    if (!ui->st.level || ui->win_phase != WIN_IDLE) return;
    if (catcube_failed(&ui->st)) {
        show_fail();
        return;
    }

    // 方向:单击前进一步,双击后退一步(上键 = 左右,下键 = 上下,都会绕回)
    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        const int step = (ev == BSP_BTN_CLICK) ? 1 : -1;
        if (btn == BSP_BTN_UP) {
            catcube_move(&ui->st, 0, step);
        } else {
            catcube_move(&ui->st, step, 0);
        }
        cat_audio_play(CAT_SND_MOVE, 0);
        refresh_play();
        return;
    }

    if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
        const uint8_t cr = (uint8_t)(catcube_cursor(&ui->st) / ui->st.level->w);
        const uint8_t cc = (uint8_t)(catcube_cursor(&ui->st) % ui->st.level->w);
        const uint8_t region = catcube_region_at(&ui->st, cr, cc);
        catcube_toggle_x(&ui->st);
        cat_audio_play(CAT_SND_X, 0);
        refresh_play();
        // 把某个色块排空、但里面还没有猫 —— 这一块必然缺一只猫,提醒一下
        if (catcube_region_exhausted(&ui->st, region)) {
            lv_label_set_text(ui->pl_hint, CAT_TOAST_REGION);
            lv_obj_set_style_text_color(ui->pl_hint, lv_color_hex(CLR_DANGER), 0);
            ui->toast_ms = 1500;
            cat_audio_play(CAT_SND_WRONG, 0);
        }
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        menu_set_page(0);
        overlay_show(OV_MENU);
        return;
    }
    if (ev == BSP_BTN_DOUBLE && btn == BSP_BTN_OK) {
        const catcube_act_t act = catcube_toggle_cat(&ui->st);
        switch (act) {
            case CATCUBE_ACT_PLACED: {
                cat_audio_play(CAT_SND_CAT, 0);
                ui->sweep_ms = 0;
                ui->sweep_active = true;
                ui->sweep_r = (int)(catcube_cursor(&ui->st) / ui->st.level->w);
                ui->sweep_c = (int)(catcube_cursor(&ui->st) % ui->st.level->w);
                int swept = 0;
                const int cells = ui->st.level->w * ui->st.level->h;
                for (int i = 0; i < cells; i++) {
                    if (catcube_mark_at(&ui->st, (uint8_t)i) == CATCUBE_MARK_X &&
                        catcube_mark_is_auto(&ui->st, (uint8_t)i)) {
                        swept++;
                    }
                }
                cat_audio_play(CAT_SND_SWEEP, swept > 0 ? 4 : 1);
                refresh_play();
                if (catcube_is_solved(&ui->st)) {
                    cat_audio_play(CAT_SND_ALL, 0);
                    start_win();
                }
                break;
            }
            case CATCUBE_ACT_REMOVED:
                cat_audio_play(CAT_SND_X, 0);
                refresh_play();
                break;
            case CATCUBE_ACT_WRONG:
                cat_audio_play(CAT_SND_WRONG, 0);
                ui->shake_idx = (int)catcube_cursor(&ui->st);
                ui->shake_ms = 320;
                refresh_play();
                if (catcube_failed(&ui->st)) show_fail();
                break;
            case CATCUBE_ACT_BLOCKED:
            case CATCUBE_ACT_LIMIT:
                cat_audio_play(CAT_SND_WRONG, 0);
                refresh_play();
                break;
            default:
                refresh_play();
                break;
        }
        return;
    }
}

static void key_volume(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        int vol = ui->hooks && ui->hooks->volume_get ? ui->hooks->volume_get() : 0;
        const int step = (ev == BSP_BTN_CLICK) ? 10 : -10;
        vol += (btn == BSP_BTN_UP) ? step : -step;
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        if (ui->hooks && ui->hooks->volume_set) ui->hooks->volume_set(vol);
        cat_audio_play(CAT_SND_MOVE, 0); // 调音量时给个即时反馈
        lv_obj_invalidate(ui->vol_page);
        return;
    }
    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        ui->menu_page = 1;
        ui->menu_sel = 1; // 停在"音量控制"上
        overlay_show(OV_MENU);
    }
}

void catcube_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    cat_ui_t *ui = &s_ui;
    if (ui->overlay == OV_VOLUME) {
        key_volume(btn, ev);
        return;
    }
    if (ui->overlay == OV_TUTORIAL) {
        if ((ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) || (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) ||
            (ev == BSP_BTN_DOUBLE && btn == BSP_BTN_OK)) {
            const bool back_to_menu = ui->tutorial_from_menu;
            ui->tutorial_from_menu = false;
            overlay_show(back_to_menu ? OV_MENU : OV_NONE);
        }
        return;
    }
    if (ui->overlay == OV_MENU) {
        key_menu(btn, ev);
        return;
    }
    if (ui->overlay == OV_WIN) {
        key_win(btn, ev);
        return;
    }
    if (ui->overlay == OV_FAIL) {
        key_fail(btn, ev);
        return;
    }
    if (ui->page == PAGE_LEVELS) key_levels(btn, ev);
    else key_play(btn, ev);
}

void catcube_ui_tick(void) {
    cat_ui_t *ui = &s_ui;
    if (ui->page == PAGE_LEVELS) refresh_battery(ui->lv_battery);
    else refresh_battery(ui->pl_battery);
}

void catcube_ui_init(const catcube_ui_hooks_t *hooks) {
    cat_ui_t *ui = &s_ui;
    memset(ui, 0, sizeof(*ui));
    ui->hooks = hooks;
    ui->cell = 16;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_levels_page();
    build_play_page();
    build_overlays();
    catcube_ui_show_levels();
}

void catcube_ui_deinit(void) {
    cat_ui_t *ui = &s_ui;
    lv_obj_t *scr = lv_screen_active();
    if (scr) lv_obj_clean(scr);
    memset(ui, 0, sizeof(*ui));
}

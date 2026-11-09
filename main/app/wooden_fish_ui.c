// main/app/wooden_fish_ui.c —— 功德木鱼界面实现。
//
// 设计取向:深色暖调("夜里点一盏灯")、木鱼占据视觉中心、功德数用大号数字,
// 敲击反馈走"物体被敲到"的物理感(缩放 + 光晕 + 上浮的 +1),不使用基线 demo
// 的菜单/卡片/吉祥物视觉。
//
// 动画不用 lv_anim:一个 16ms 定时器 + 自己算插值,时序完全可控,也方便在
// 没有硬件时按"帧"推理。
#include "app/wooden_fish_ui.h"

#include "app/app_fonts.h"
#include "app/app_strings.h"

#define WF_COLOR_BG_TOP    0x1A1310
#define WF_COLOR_BG_BOTTOM 0x2A1A13
#define WF_COLOR_WOOD      0x7A2E24
#define WF_COLOR_WOOD_HI   0x96402F
#define WF_COLOR_RIM       0xC4703C
#define WF_COLOR_AMBER     0xE8B04B
#define WF_COLOR_DIM       0xB9A18B
#define WF_COLOR_SLIT      0x2A120E
#define WF_COLOR_OVERLAY   0x000000

#define WF_HIT_MS        260   // 一次敲击动画时长
#define WF_FRAME_MS      16
#define WF_COMBO_HIDE_MS 1500  // 与 merit_logic 的连击窗口一致

typedef struct {
    uint32_t started_ms;
    bool running;
} wf_hit_anim_t;

static lv_obj_t *s_knock_scr;
static lv_obj_t *s_stats_scr;
static lv_obj_t *s_confirm;

static lv_obj_t *s_fish;
static lv_obj_t *s_fish_inner;
static lv_obj_t *s_fish_slit;
static lv_obj_t *s_ring;
static lv_obj_t *s_plus_one;
static lv_obj_t *s_count;
static lv_obj_t *s_combo;
static lv_obj_t *s_battery;
static lv_obj_t *s_mute_badge;

static lv_obj_t *s_stat_session;
static lv_obj_t *s_stat_total;
static lv_obj_t *s_stat_combo;
static lv_obj_t *s_stat_sound;
static lv_obj_t *s_stat_mute;

static lv_timer_t *s_anim_timer;
static wf_hit_anim_t s_anim;
static uint32_t s_combo_hide_at_ms;   // 0 表示当前没有待隐藏的连击标签

static const char *const SOUND_NAMES[] = {APP_SOUND_WOOD, APP_SOUND_BELL, APP_SOUND_DRUM};

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

// 屏幕中心与木鱼中心(屏幕坐标系)。240x320 是 BSP 的固定面板尺寸;
// 木鱼顶部偏移 64、高 112,故中心在 y = 64 + 56。
#define WF_SCREEN_CENTER_X 120
#define WF_FISH_CENTER_Y   120

// 光晕以木鱼为中心、按给定直径摆放。
static void set_ring_size(int32_t diameter) {
    lv_obj_set_size(s_ring, diameter, diameter);
    lv_obj_set_pos(s_ring, WF_SCREEN_CENTER_X - diameter / 2,
                   WF_FISH_CENTER_Y - diameter / 2);
}

static lv_obj_t *make_block(lv_obj_t *parent, int32_t w, int32_t h, uint32_t color,
                            int32_t radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// 深色渐变背景:从顶部近黑过渡到底部木色,像一盏暖光落在木鱼上。
static void apply_background(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(WF_COLOR_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(WF_COLOR_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

static void anim_reset(void) {
    if (!s_fish) return;
    lv_obj_set_style_transform_scale_x(s_fish, 256, 0);
    lv_obj_set_style_transform_scale_y(s_fish, 256, 0);
    lv_obj_set_style_opa(s_ring, LV_OPA_TRANSP, 0);
    set_ring_size(96);
    lv_obj_set_style_opa(s_plus_one, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(s_plus_one, 0, 0);
    lv_obj_set_style_transform_scale_x(s_count, 256, 0);
    lv_obj_set_style_transform_scale_y(s_count, 256, 0);
}

// 三角波插值:0 → 1 → 0,用来做"打一下再回弹"。
static int32_t triangle(uint32_t elapsed, uint32_t total) {
    if (total == 0) return 0;
    uint32_t half = total / 2;
    if (elapsed >= total) return 0;
    if (elapsed < half) return (int32_t)((elapsed * 1000u) / half);
    return (int32_t)(((total - elapsed) * 1000u) / half);
}

static void anim_timer_cb(lv_timer_t *timer) {
    (void)timer;
    const uint32_t now = lv_tick_get();

    // 连击标签按时间窗隐藏:敲击动画只有 260ms,但连击窗口是 1500ms,
    // 所以定时器必须活到窗口结束,不能跟着动画一起停。
    if (s_combo_hide_at_ms != 0 && (int32_t)(now - s_combo_hide_at_ms) >= 0) {
        s_combo_hide_at_ms = 0;
        if (s_combo) lv_obj_add_flag(s_combo, LV_OBJ_FLAG_HIDDEN);
    }

    if (!s_anim.running) {
        if (s_combo_hide_at_ms == 0 && s_anim_timer) lv_timer_pause(s_anim_timer);
        return;
    }

    const uint32_t elapsed = now - s_anim.started_ms;
    if (elapsed >= WF_HIT_MS) {
        s_anim.running = false;
        anim_reset();
        if (s_combo_hide_at_ms == 0 && s_anim_timer) lv_timer_pause(s_anim_timer);
        return;
    }

    const int32_t phase = triangle(elapsed, WF_HIT_MS);  // 0..1000..0

    // 木鱼:敲击瞬间放大 6%,然后回落。
    const int32_t fish_scale = 256 + (phase * 16) / 1000;
    lv_obj_set_style_transform_scale_x(s_fish, fish_scale, 0);
    lv_obj_set_style_transform_scale_y(s_fish, fish_scale, 0);

    // 光晕:从 96px 扩散到 150px,同时淡出。
    set_ring_size(96 + (phase * 54) / 1000);
    const lv_opa_t ring_opa = (lv_opa_t)(120 - (phase * 120) / 1000);
    lv_obj_set_style_opa(s_ring, ring_opa, 0);

    // +1:上浮 34px 并淡出。
    lv_obj_set_style_translate_y(s_plus_one, -(phase * 34) / 1000, 0);
    const lv_opa_t plus_opa = (lv_opa_t)(255 - (phase * 255) / 1000);
    lv_obj_set_style_opa(s_plus_one, plus_opa, 0);

    // 数字:轻微跳动,比木鱼快一点,读起来像"计数被顶了一下"。
    const int32_t count_phase = triangle(elapsed, WF_HIT_MS / 2);
    const int32_t count_scale = 256 + (count_phase * 44) / 1000;
    lv_obj_set_style_transform_scale_x(s_count, count_scale, 0);
    lv_obj_set_style_transform_scale_y(s_count, count_scale, 0);
}

static void anim_start(void) {
    s_anim.started_ms = lv_tick_get();
    s_anim.running = true;
    if (s_anim_timer) {
        lv_timer_resume(s_anim_timer);
        lv_timer_ready(s_anim_timer);
    }
}

// ---------------------------------------------------------------------------
// 敲击页
// ---------------------------------------------------------------------------
static void build_knock_screen(void) {
    s_knock_scr = lv_obj_create(NULL);
    apply_background(s_knock_scr);

    lv_obj_t *title = make_label(s_knock_scr, app_font_title(), WF_COLOR_AMBER, APP_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_battery = make_label(s_knock_scr, app_font_body(), WF_COLOR_DIM, APP_BATTERY_UNKNOWN);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -12, 12);

    // 木鱼主体:三层椭圆(外体、内壁、鱼口),整体参与敲击缩放。
    s_fish = make_block(s_knock_scr, 160, 112, WF_COLOR_WOOD, LV_RADIUS_CIRCLE);
    lv_obj_align(s_fish, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_border_width(s_fish, 3, 0);
    lv_obj_set_style_border_color(s_fish, lv_color_hex(WF_COLOR_RIM), 0);
    lv_obj_set_style_shadow_width(s_fish, 18, 0);
    lv_obj_set_style_shadow_color(s_fish, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_fish, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_y(s_fish, 6, 0);

    s_fish_inner = make_block(s_fish, 112, 70, WF_COLOR_WOOD_HI, LV_RADIUS_CIRCLE);
    lv_obj_align(s_fish_inner, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_opa(s_fish_inner, LV_OPA_60, 0);

    // 鱼口:木鱼下方的横向开口,敲击时会亮一下(靠光晕表现,不单独改色)。
    s_fish_slit = make_block(s_fish, 64, 12, WF_COLOR_SLIT, 6);
    lv_obj_align(s_fish_slit, LV_ALIGN_BOTTOM_MID, 0, -14);

    // 光晕环:只在敲击时可见。
    s_ring = make_block(s_knock_scr, 96, 96, WF_COLOR_RIM, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(WF_COLOR_AMBER), 0);
    set_ring_size(96);
    lv_obj_set_style_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_move_to_index(s_ring, 0);  // 压在木鱼后面

    s_plus_one = make_label(s_knock_scr, app_font_title(), WF_COLOR_AMBER, "+1");
    lv_obj_align(s_plus_one, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_opa(s_plus_one, LV_OPA_TRANSP, 0);

    s_count = make_label(s_knock_scr, app_font_number(), WF_COLOR_AMBER, "0");
    lv_obj_align(s_count, LV_ALIGN_TOP_MID, 0, 182);

    lv_obj_t *unit = make_label(s_knock_scr, app_font_body(), WF_COLOR_DIM, APP_UNIT);
    lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 240);

    s_combo = make_label(s_knock_scr, app_font_body(), WF_COLOR_RIM, APP_COMBO_PREFIX);
    lv_obj_align(s_combo, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_add_flag(s_combo, LV_OBJ_FLAG_HIDDEN);

    s_mute_badge = make_label(s_knock_scr, app_font_body(), WF_COLOR_RIM, APP_MUTED);
    lv_obj_align(s_mute_badge, LV_ALIGN_TOP_LEFT, 12, 38);
    lv_obj_add_flag(s_mute_badge, LV_OBJ_FLAG_HIDDEN);

    // 两行提示分开放,避免为了分隔符去引入字库里没有的码点。
    lv_obj_t *hint = make_label(s_knock_scr, app_font_body(), WF_COLOR_DIM, APP_HINT_KNOCK);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -36);

    lv_obj_t *hint2 = make_label(s_knock_scr, app_font_body(), WF_COLOR_DIM, APP_HINT_RESET);
    lv_obj_align(hint2, LV_ALIGN_BOTTOM_MID, 0, -16);

    // 归零确认浮层(默认隐藏)。
    s_confirm = lv_obj_create(s_knock_scr);
    lv_obj_remove_style_all(s_confirm);
    lv_obj_set_size(s_confirm, 240, 320);
    lv_obj_set_pos(s_confirm, 0, 0);
    lv_obj_set_style_bg_color(s_confirm, lv_color_hex(WF_COLOR_OVERLAY), 0);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_70, 0);
    lv_obj_clear_flag(s_confirm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = make_block(s_confirm, 200, 128, 0x2A1C16, 16);
    lv_obj_center(panel);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(WF_COLOR_AMBER), 0);

    lv_obj_t *q = make_label(panel, app_font_title(), WF_COLOR_AMBER, APP_RESET_TITLE);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *yes = make_label(panel, app_font_body(), WF_COLOR_DIM, APP_RESET_CONFIRM);
    lv_obj_align(yes, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t *no = make_label(panel, app_font_body(), WF_COLOR_DIM, APP_RESET_CANCEL);
    lv_obj_align(no, LV_ALIGN_TOP_MID, 0, 92);

    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// 功德簿(统计页)
// ---------------------------------------------------------------------------
static lv_obj_t *stat_row(lv_obj_t *parent, const char *label, int32_t y,
                          lv_obj_t **value_out) {
    lv_obj_t *row = make_block(parent, 208, 38, 0x241812, 10);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_t *name = make_label(row, app_font_body(), WF_COLOR_DIM, label);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *value = make_label(row, app_font_title(), WF_COLOR_AMBER, "0");
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -10, 0);
    if (value_out) *value_out = value;
    return row;
}

static void build_stats_screen(void) {
    s_stats_scr = lv_obj_create(NULL);
    apply_background(s_stats_scr);

    lv_obj_t *title = make_label(s_stats_scr, app_font_title(), WF_COLOR_AMBER, APP_STATS_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    stat_row(s_stats_scr, APP_SESSION_LABEL, 58, &s_stat_session);
    stat_row(s_stats_scr, APP_TOTAL_LABEL, 102, &s_stat_total);
    stat_row(s_stats_scr, APP_BEST_COMBO_LABEL, 146, &s_stat_combo);
    stat_row(s_stats_scr, APP_SOUND_LABEL, 190, &s_stat_sound);
    stat_row(s_stats_scr, APP_MUTE_LABEL, 234, &s_stat_mute);

    lv_obj_t *hint = make_label(s_stats_scr, app_font_body(), WF_COLOR_DIM, APP_HINT_BACK);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void wf_ui_init(void) {
    if (s_knock_scr) return;
    app_fonts_init();
    build_knock_screen();
    build_stats_screen();

    s_anim_timer = lv_timer_create(anim_timer_cb, WF_FRAME_MS, NULL);
    if (s_anim_timer) lv_timer_pause(s_anim_timer);

    anim_reset();
    lv_screen_load(s_knock_scr);
}

void wf_ui_destroy(void) {
    if (s_anim_timer) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
    }
    if (s_knock_scr) {
        lv_obj_delete(s_knock_scr);
        s_knock_scr = NULL;
    }
    if (s_stats_scr) {
        lv_obj_delete(s_stats_scr);
        s_stats_scr = NULL;
    }
    s_fish = s_fish_inner = s_fish_slit = s_ring = s_plus_one = NULL;
    s_count = s_combo = s_battery = s_mute_badge = s_confirm = NULL;
    s_stat_session = s_stat_total = s_stat_combo = s_stat_sound = s_stat_mute = NULL;
    s_anim.running = false;
}

void wf_ui_show_knock(void) {
    if (!s_knock_scr) return;
    lv_screen_load(s_knock_scr);
}

void wf_ui_show_stats(void) {
    if (!s_stats_scr) return;
    lv_screen_load(s_stats_scr);
}

void wf_ui_set_confirm(bool visible) {
    if (!s_confirm) return;
    if (visible) {
        lv_obj_clear_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_confirm);
    } else {
        lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    }
}

void wf_ui_refresh(const wf_view_t *view) {
    if (!view || !s_count) return;

    lv_label_set_text_fmt(s_count, "%lu", (unsigned long)view->total);

    if (s_stat_session) lv_label_set_text_fmt(s_stat_session, "%lu", (unsigned long)view->session);
    if (s_stat_total) lv_label_set_text_fmt(s_stat_total, "%lu", (unsigned long)view->total);
    if (s_stat_combo) lv_label_set_text_fmt(s_stat_combo, "%lu", (unsigned long)view->best_combo);
    if (s_stat_sound) {
        uint8_t preset = view->preset < 3 ? view->preset : 0;
        lv_label_set_text(s_stat_sound, SOUND_NAMES[preset]);
    }
    if (s_stat_mute) lv_label_set_text(s_stat_mute, view->muted ? APP_OFF : APP_ON);

    if (view->battery_soc >= 0 && view->battery_soc <= 100) {
        lv_label_set_text_fmt(s_battery, "%d%%", view->battery_soc);
    } else {
        lv_label_set_text(s_battery, APP_BATTERY_UNKNOWN);
    }

    if (view->muted) {
        lv_obj_clear_flag(s_mute_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_mute_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void wf_ui_play_hit(uint32_t combo) {
    if (!s_count || !s_combo) return;

    if (combo >= 2) {
        lv_label_set_text_fmt(s_combo, APP_COMBO_PREFIX " x%lu", (unsigned long)combo);
        lv_obj_clear_flag(s_combo, LV_OBJ_FLAG_HIDDEN);
        s_combo_hide_at_ms = lv_tick_get() + WF_COMBO_HIDE_MS;
    } else {
        lv_obj_add_flag(s_combo, LV_OBJ_FLAG_HIDDEN);
        s_combo_hide_at_ms = 0;
    }

    anim_start();
}

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lv_port.h"
#include "nav.h"
#include "sensor_model.h"
#include "sensor_view.h"
#include "view_data.h"

static const char *TAG = "sensor_view";

#define BUF_SIZE          32
#define CARD_W            214
#define CARD_H            100
#define CARD_GAP_X        8
#define CARD_GAP_Y        8
#define CARD_LEFT_MARGIN  22
#define HEADER_H          75
#define VOC_CARD_W        (CARD_W * 2 + CARD_GAP_X)   /* 436 px */
#define VOC_CARD_H        150

/* Row y-offsets (all relative to tile top) */
#define ROW0_Y  (HEADER_H + CARD_GAP_Y)                                /* VOC   */
#define ROW1_Y  (ROW0_Y + VOC_CARD_H + CARD_GAP_Y)                    /* Temp + Hum */
#define ROW2_Y  (ROW1_Y + CARD_H + CARD_GAP_Y)                        /* PM2.5 + PM1.0 */
/* ROW2 bottom = ROW2_Y + CARD_H = 83+150+8+100+8+100 = 449 — fits in 480, no scroll */

LV_FONT_DECLARE(ui_font_font0);

/* ── VOC 3-state system ─────────────────────────────────────────────────────
 * Maps voc_alert: 0 → Good, 1-2 → Moderate, 3 → Poor
 */
#define N_VOC_STATES  3
#define SEG_MARGIN    14   /* left/right margin inside VOC card */
#define SEG_GAP       6    /* gap between segments */
#define SEG_W         132  /* (436 - 2*14 - 2*6) / 3 */
#define SEG_H         38
#define SEG_Y         (VOC_CARD_H - SEG_H - 8)

static const uint32_t VOC_STATE_COLORS[N_VOC_STATES] = {
    0x4F9E52,   /* Good     — green  */
    0xD76D46,   /* Moderate — orange */
    0xC0392B,   /* Poor     — red    */
};
static const char *VOC_STATE_LABELS[N_VOC_STATES] = {"Good", "Moderate", "Poor"};

static lv_obj_t *s_voc_value_lbl  = NULL;
static lv_obj_t *s_voc_state_lbl  = NULL;
static lv_obj_t *s_voc_seg[N_VOC_STATES];
static lv_obj_t *s_voc_seg_lbl[N_VOC_STATES];
static bool      s_warming_up     = true;   /* suppress VOC value until status event fires */

static int _voc_state(int alert)
{
    if (alert <= 0) return 0;
    if (alert <= 2) return 1;
    return 2;
}

/* ── Regular card specs (PM2.5, PM1.0, Temp, Humidity) ─────────────────── */

typedef struct {
    enum sensor_data_type type;
    const char           *name;
    const char           *unit;
    const char           *fmt;
    uint32_t              accent;
    int32_t               x, y, w, h;
    lv_obj_t             *lbl_data;
} CardSpec;

static CardSpec s_cards[] = {
    /* Row 1 */
    { SEN54_SENSOR_TEMP,     "Temp",     "\xc2\xb0" "C",              "%.1f",
      0xECBF41, CARD_LEFT_MARGIN,                  ROW1_Y, CARD_W, CARD_H, NULL },
    { SEN54_SENSOR_HUMIDITY, "Humidity", "%",                          "%.0f",
      0x52AAE5, CARD_LEFT_MARGIN + CARD_W + CARD_GAP_X, ROW1_Y, CARD_W, CARD_H, NULL },

    /* Row 2 */
    { SEN54_SENSOR_PM2_5, "PM 2.5", "ug/m3",        "%.1f",
      0x9B59B6, CARD_LEFT_MARGIN,                  ROW2_Y, CARD_W, CARD_H, NULL },
    { SEN54_SENSOR_PM1_0, "PM 1.0", "ug/m3",        "%.1f",
      0x16A085, CARD_LEFT_MARGIN + CARD_W + CARD_GAP_X, ROW2_Y, CARD_W, CARD_H, NULL },
};
#define N_CARDS (sizeof(s_cards) / sizeof(s_cards[0]))

/* ── Card builders ──────────────────────────────────────────────────────── */

static void _card_base_style(lv_obj_t *card, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x282828), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
}

static void _create_regular_card(lv_obj_t *tile, CardSpec *spec)
{
    lv_obj_t *card = lv_obj_create(tile);
    _card_base_style(card, spec->x, spec->y, spec->w, spec->h);

    lv_color_t accent = lv_color_hex(spec->accent);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, spec->name);
    lv_obj_set_style_text_color(name, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(name, 10, 6);

    lv_obj_t *data = lv_label_create(card);
    lv_label_set_text(data, "---");
    lv_obj_set_style_text_color(data, accent, 0);
    lv_obj_set_style_text_font(data, &lv_font_montserrat_36, 0);
    lv_obj_align(data, LV_ALIGN_CENTER, 0, 5);
    spec->lbl_data = data;

    lv_obj_t *unit = lv_label_create(card);
    lv_label_set_text(unit, spec->unit);
    lv_obj_set_style_text_color(unit, accent, 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
}

static void _create_voc_card(lv_obj_t *tile)
{
    lv_obj_t *card = lv_obj_create(tile);
    _card_base_style(card, CARD_LEFT_MARGIN, ROW0_Y, VOC_CARD_W, VOC_CARD_H);

    /* ── Name label ── */
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, "VOC Index");
    lv_obj_set_style_text_color(name, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(name, 14, 7);

    /* ── Large numeric value (left side) ── */
    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, "---");
    lv_obj_set_style_text_color(val, lv_color_hex(0x555555), 0);  /* gray until warm-up done */
    lv_obj_set_style_text_font(val, &ui_font_font0, 0);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -23);
    s_voc_value_lbl = val;

    /* ── State / calibrating label (right side) ── */
    lv_obj_t *st = lv_label_create(card);
    lv_label_set_text(st, "Calibrating");
    lv_obj_set_style_text_color(st, lv_color_hex(0xECBF41), 0);   /* amber while warming */
    lv_obj_set_style_text_font(st, &lv_font_montserrat_26, 0);
    lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -14, 46);
    s_voc_state_lbl = st;

    /* ── 3 state segments (Poor=left, Moderate=center, Good=right) ── */
    for (int i = 0; i < N_VOC_STATES; i++) {
        int32_t sx = SEG_MARGIN + (N_VOC_STATES - 1 - i) * (SEG_W + SEG_GAP);

        lv_obj_t *seg = lv_obj_create(card);
        lv_obj_set_size(seg, SEG_W, SEG_H);
        lv_obj_set_pos(seg, sx, SEG_Y);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(seg, 6, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        s_voc_seg[i] = seg;

        lv_obj_t *lbl = lv_label_create(seg);
        lv_label_set_text(lbl, VOC_STATE_LABELS[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
        lv_obj_center(lbl);
        s_voc_seg_lbl[i] = lbl;
    }
}

/* ── Header ─────────────────────────────────────────────────────────────── */

static void _create_header(lv_obj_t *tile)
{
    lv_obj_t *hdr = lv_obj_create(tile);
    lv_obj_set_size(hdr, 480, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Seeed Monitor");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &ui_font_font0, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
}

/* ── Shared VOC segment update (must hold LVGL lock on entry) ───────────── */

static void _update_voc_state(int voc_state_idx, bool warming_up)
{
    for (int i = 0; i < N_VOC_STATES; i++) {
        if (!s_voc_seg[i]) continue;
        bool active = !warming_up && (i == voc_state_idx);

        lv_obj_set_style_bg_color(s_voc_seg[i],
            active ? lv_color_hex(VOC_STATE_COLORS[i]) : lv_color_hex(0x2A2A2A), 0);

        if (s_voc_seg_lbl[i]) {
            lv_obj_set_style_text_color(s_voc_seg_lbl[i],
                active ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x555555), 0);
        }
    }

    if (s_voc_state_lbl) {
        if (warming_up) {
            lv_label_set_text(s_voc_state_lbl, "Calibrating");
            lv_obj_set_style_text_color(s_voc_state_lbl, lv_color_hex(0xECBF41), 0);
        } else {
            lv_label_set_text(s_voc_state_lbl, VOC_STATE_LABELS[voc_state_idx]);
            lv_obj_set_style_text_color(s_voc_state_lbl,
                lv_color_hex(VOC_STATE_COLORS[voc_state_idx]), 0);
        }
    }

    if (s_voc_value_lbl) {
        uint32_t vc = warming_up ? 0x555555 : VOC_STATE_COLORS[voc_state_idx];
        lv_obj_set_style_text_color(s_voc_value_lbl, lv_color_hex(vc), 0);
    }
}

/* ── Event handlers ─────────────────────────────────────────────────────── */

void view_event_update_present_sensorData(void *handler_args, esp_event_base_t base,
                                           int32_t id, void *event_data)
{
    if (id != VIEW_EVENT_SENSOR_DATA) return;

    struct view_data_sensor_data *p = (struct view_data_sensor_data *)event_data;
    if (!p) return;

    /* VOC value update — suppress during warm-up (sensor outputs 0 and is unreliable) */
    if (p->sensor_type == SEN54_SENSOR_VOC_IDX) {
        if (!s_warming_up) {
            char buf[BUF_SIZE];
            snprintf(buf, sizeof(buf), "%.0f", p->value);
            lv_port_sem_take();
            if (s_voc_value_lbl) lv_label_set_text(s_voc_value_lbl, buf);
            lv_port_sem_give();
        }
        return;
    }

    /* Regular cards */
    CardSpec *spec = NULL;
    for (size_t i = 0; i < N_CARDS; i++) {
        if (s_cards[i].type == p->sensor_type) {
            spec = &s_cards[i];
            break;
        }
    }
    if (!spec || !spec->lbl_data) return;

    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), spec->fmt, p->value);
    lv_port_sem_take();
    lv_label_set_text(spec->lbl_data, buf);
    lv_port_sem_give();
}

static void _sen5x_status_handler(void *handler_args, esp_event_base_t base,
                                   int32_t id, void *event_data)
{
    if (id != VIEW_EVENT_SEN5X_STATUS || !event_data) return;

    struct view_data_sen5x_status *st = (struct view_data_sen5x_status *)event_data;
    int state = _voc_state(st->voc_alert);
    s_warming_up = st->warming_up;

    lv_port_sem_take();
    _update_voc_state(state, st->warming_up);

    lv_port_sem_give();
}

/* ── Init ───────────────────────────────────────────────────────────────── */

void view_sensor_init(void)
{
    lv_port_sem_take();
    lv_obj_t *tile = nav_get_tile(NAV_TILE_SEN5X);
    if (!tile) {
        lv_port_sem_give();
        ESP_LOGE(TAG, "SEN5X tile not initialised");
        return;
    }

    /* No scroll needed: total content height ≈ 449 px < 480 px */
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    _create_header(tile);
    _create_voc_card(tile);
    for (size_t i = 0; i < N_CARDS; i++) {
        _create_regular_card(tile, &s_cards[i]);
    }

    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
        view_event_update_present_sensorData, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SEN5X_STATUS,
        _sen5x_status_handler, NULL, NULL));
}

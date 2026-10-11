#pragma once
#include <lvgl.h>

// One shared, opaque focus style for every page, independent of the palette.
// Applied after LVGL's base theme, including controls created dynamically.
namespace selection_outline {
static lv_style_t style;
static lv_theme_t theme;
static void apply(lv_theme_t *, lv_obj_t *object) {
    for (lv_state_t state : {LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_EDITED}) {
        lv_obj_add_style(object, &style, LV_PART_MAIN | state);
        if (lv_obj_check_type(object, &lv_buttonmatrix_class))
            lv_obj_add_style(object, &style, LV_PART_ITEMS | state);
    }
}
static void install(lv_display_t *display) {
    lv_style_init(&style);
    lv_style_set_outline_color(&style, lv_color_hex(0xFFFF00));
    lv_style_set_outline_opa(&style, LV_OPA_COVER);
    lv_style_set_outline_width(&style, 2);
    lv_style_set_outline_pad(&style, 0);
    lv_theme_t *base = lv_display_get_theme(display);
    if (base) theme = *base;
    theme.disp = display;
    lv_theme_set_parent(&theme, base);
    lv_theme_set_apply_cb(&theme, apply);
    lv_display_set_theme(display, &theme);
}
} // namespace selection_outline

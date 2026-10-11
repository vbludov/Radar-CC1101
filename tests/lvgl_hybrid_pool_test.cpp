#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0 &&
                  LVGL_VERSION_PATCH == 0,
              "Hybrid pool regression must use firmware-pinned LVGL 9.0.0");

namespace {
constexpr size_t kLayerBytes = 320U * 7U * 4U;
constexpr size_t kExtraPoolBytes = 128U * 1024U;

alignas(std::max_align_t) uint8_t extraPool[kExtraPoolBytes];
alignas(std::max_align_t) uint8_t drawBuffer[320U * 20U * 4U];
unsigned noMemoryWarnings;
unsigned checks;
unsigned failures;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void logCallback(lv_log_level_t, const char *message) {
    if (message && std::strstr(message, "No memory")) ++noMemoryWarnings;
}

void flushCallback(lv_display_t *display, const lv_area_t *, uint8_t *) {
    lv_display_flush_ready(display);
}

void styleListButton(lv_obj_t *button) {
    lv_obj_set_height(button, 26);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x181c24),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(button, lv_color_hex(0xe8eef7),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x27364b),
                              LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x38bdf8),
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 1,
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x27364b),
                              LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(button, lv_color_hex(0x38bdf8),
                                  LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(button, 1,
                                  LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(button, 8, LV_PART_MAIN);
}

struct HybridPage {
    lv_obj_t *screen;
    lv_obj_t *rows[30];
    lv_group_t *group;
};

// The production path keeps the WiFi family menu alive behind the scanner so
// Back can return to it.  Include that retained allocation load; omitting it
// leaves a misleadingly large contiguous block in the 64 KB host pool.
void createRetainedWifiMenu() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi Tools");
    lv_obj_set_pos(title, 8, 5);
    lv_obj_t *list = lv_list_create(screen);
    lv_obj_set_size(list, 320, 108);
    lv_obj_set_pos(list, 0, 28);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
    lv_group_t *group = lv_group_create();
    // Four representative rows supply the retained-menu pressure needed by
    // the host allocator.  The target retains all 13, so this is conservative.
    for (unsigned i = 0; i < 4; ++i) {
        char text[64];
        std::snprintf(text, sizeof(text), "WiFi scanner tool %u", i + 1U);
        lv_obj_t *row = lv_list_add_btn(list, nullptr, text);
        styleListButton(row);
        lv_obj_set_height(row, 30);
        lv_group_add_obj(group, row);
    }
    lv_obj_t *back = lv_button_create(screen);
    lv_obj_set_size(back, 50, 28);
    lv_obj_set_pos(back, 4, 140);
    lv_obj_t *label = lv_label_create(back);
    lv_label_set_text(label, "Back");
    lv_obj_center(label);
    lv_group_add_obj(group, back);
    lv_screen_load(screen);
}

HybridPage createHybridPage() {
    HybridPage page{};
    page.screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(page.screen, lv_color_hex(0x090c12), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(page.screen);
    lv_label_set_text(title, LV_SYMBOL_WARNING "  Flock Hybrid");
    lv_obj_set_pos(title, 8, 5);

    lv_obj_t *status = lv_label_create(page.screen);
    lv_label_set_text(status, "Continuous scan  WiFi + BLE  30 matches");
    lv_obj_set_pos(status, 8, 30);

    lv_obj_t *list = lv_list_create(page.screen);
    lv_obj_set_size(list, 320, 88);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x090c12), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 1, LV_PART_MAIN);

    page.group = lv_group_create();
    for (unsigned i = 0; i < 30; ++i) {
        char rowText[180];
        std::snprintf(rowText, sizeof(rowText),
                      "%s Sensor [%s] -%u dBm x%u\n"
                      "02:11:22:33:44:%02X  %s evidence  %us",
                      (i & 1U) ? "BLE" : "WiFi", (i % 3U) ? "MED" : "HIGH",
                      35U + i, i + 1U, i, (i & 1U) ? "manufacturer" : "probe",
                      i * 3U);
        page.rows[i] = lv_list_add_btn(list, nullptr, rowText);
        styleListButton(page.rows[i]);
        lv_group_add_obj(page.group, page.rows[i]);
    }

    const char *controls[] = {"Back", "Continuous", "Start", "S", "L"};
    const int16_t x[] = {4, 58, 160, 234, 274};
    const int16_t width[] = {50, 98, 70, 36, 36};
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t *button = lv_button_create(page.screen);
        lv_obj_set_size(button, width[i], 28);
        lv_obj_set_pos(button, x[i], 140);
        lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, controls[i]);
        lv_obj_center(label);
        lv_group_add_obj(page.group, button);
    }

    lv_group_focus_obj(page.rows[7]);
    return page;
}
}  // namespace

int main(int argc, char **argv) {
    const bool expanded = argc == 2 && std::strcmp(argv[1], "expanded") == 0;
    const bool baseline = argc == 2 && std::strcmp(argv[1], "baseline") == 0;
    if (!expanded && !baseline) {
        std::fprintf(stderr, "usage: %s baseline|expanded\n", argv[0]);
        return EXIT_FAILURE;
    }

    lv_init();
    if (expanded) {
        check(lv_mem_add_pool(extraPool, sizeof(extraPool)) != nullptr,
              "128 KB supplemental pool registered");
    }

    lv_display_t *display = lv_display_create(320, 170);
    check(display != nullptr, "display created");
    lv_display_set_default(display);
    lv_display_set_flush_cb(display, flushCallback);
    lv_display_set_buffers(display, drawBuffer, nullptr, sizeof(drawBuffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    createRetainedWifiMenu();
    HybridPage page = createHybridPage();
    lv_screen_load(page.screen);
    lv_obj_update_layout(page.screen);
    check(lv_group_get_focused(page.group) == page.rows[7],
          "Hybrid result focus identity is stable");

    lv_mem_monitor_t monitor{};
    lv_mem_monitor(&monitor);
    std::printf("%s: free=%u largest=%u used=%u%%\n",
                expanded ? "expanded" : "baseline",
                static_cast<unsigned>(monitor.free_size),
                static_cast<unsigned>(monitor.free_biggest_size),
                static_cast<unsigned>(monitor.used_pct));

    void *layer = lv_malloc(kLayerBytes);
    if (baseline) {
        check(monitor.free_biggest_size < kLayerBytes,
              "64 KB pool largest block is below measured 8960-byte layer");
        check(layer == nullptr,
              "64 KB pool rejects the measured 8960-byte layer allocation");
    } else {
        check(monitor.free_biggest_size >= kLayerBytes,
              "supplemental pool has an 8960-byte contiguous block");
        check(layer != nullptr,
              "supplemental pool accepts the measured layer allocation");
        lv_free(layer);

        noMemoryWarnings = 0;
        lv_log_register_print_cb(logCallback);
        lv_obj_invalidate(page.rows[7]);
        lv_refr_now(display);
        check(noMemoryWarnings == 0,
              "focused 30-row Hybrid render emits no allocation warnings");
        check(lv_group_get_focused(page.group) == page.rows[7],
              "focused Hybrid identity survives rendering");
    }

    if (failures) {
        std::fprintf(stderr, "%u of %u Hybrid pool checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    std::printf("PASS: %u Hybrid %s-pool checks\n", checks,
                expanded ? "supplemental" : "baseline");
    return EXIT_SUCCESS;
}

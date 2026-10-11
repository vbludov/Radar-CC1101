#include <lvgl.h>

#include <cassert>
#include <cstdint>
#include <cstdio>

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0 &&
                  LVGL_VERSION_PATCH == 0,
              "Pwn focus regression must use firmware-pinned LVGL 9.0.0");

static constexpr int MAX_PWNS = 3;
static lv_obj_t *pwnList = nullptr;
static lv_group_t *wifiToolGroup = nullptr;
static lv_obj_t *pwnRowBtns[MAX_PWNS] = {nullptr};
static int pwnCount = 0;
static int pwnRowCount = 0;

#ifndef PWN_FOCUSED_HELPER_INCLUDE
#error "run_lvgl_tests.sh must extract the production pwnFocusedIndex helper"
#endif
#include PWN_FOCUSED_HELPER_INCLUDE

static int chooseEvictionSlot(const uint32_t lastSeen[MAX_PWNS]) {
    const int focused = pwnFocusedIndex();
    int slot = -1;
    for (int i = 0; i < pwnCount; ++i) {
        if (i == focused) continue;
        if (slot < 0 || lastSeen[i] < lastSeen[slot]) slot = i;
    }
    return slot;
}

int main() {
    lv_init();
    lv_display_t *display = lv_display_create(320, 170);
    assert(display != nullptr);
    lv_display_set_default(display);
    lv_timer_pause(lv_display_get_refr_timer(display));

    lv_obj_t *screen = lv_screen_active();
    pwnList = lv_list_create(screen);
    wifiToolGroup = lv_group_create();

    lv_obj_t *placeholder = lv_list_add_text(pwnList, "No matching beacons yet.");
    lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < MAX_PWNS; ++i) {
        pwnRowBtns[i] = lv_list_add_button(pwnList, nullptr, "result");
        lv_group_add_obj(wifiToolGroup, pwnRowBtns[i]);
    }
    pwnCount = pwnRowCount = MAX_PWNS;

    assert(lv_obj_get_child(pwnList, 0) == placeholder);
    assert(lv_obj_get_child(pwnList, 1) == pwnRowBtns[0]);
    lv_group_focus_obj(pwnRowBtns[1]);
    assert(pwnFocusedIndex() == 1);

    // The focused row is deliberately the oldest. Capacity eviction must
    // choose the next-oldest identity and leave the focused object alive.
    const uint32_t lastSeen[MAX_PWNS] = {20, 1, 10};
    assert(chooseEvictionSlot(lastSeen) == 2);
    assert(lv_group_get_focused(wifiToolGroup) == pwnRowBtns[1]);

    // The production helper bounds lookup by both model and retained-row
    // counts, preventing a stale row pointer from being treated as focused.
    pwnRowCount = 2;
    lv_group_focus_obj(pwnRowBtns[2]);
    assert(pwnFocusedIndex() == -1);

    std::puts("PASS: retained Pwn rows ignore placeholder and protect focused identity");
    return 0;
}

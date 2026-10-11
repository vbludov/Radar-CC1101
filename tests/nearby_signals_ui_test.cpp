#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>

#include "Arduino.h"
#include "BLEDevice.h"
#include "WiFi.h"
#include "../rogue-radar/known_device_types.h"

#define SCREEN_W 320
#define SCREEN_H 170
#define ENCODER_BTN 0
#define LOW 0

uint32_t fakeMillis;
void fakeArduinoDelayHook(uint32_t) {}
static int digitalRead(int) { return 1; }
static void resetInactivityTimer() {}
static void *ps_malloc(size_t size) { return std::malloc(size); }

static BLEScan fakeBleScan;
bool BLEDevice::initialized = false;
BLEScan *BLEDevice::scan = &fakeBleScan;
gap_event_handler BLEDevice::m_customGapHandler = nullptr;
FakeWiFiClass WiFi;
int esp_wifi_scan_stop() { return 0; }

int16_t FakeWiFiClass::scanComplete() const { return WIFI_SCAN_FAILED; }
void FakeWiFiClass::scanDelete() {}
int16_t FakeWiFiClass::scanNetworks(bool, bool, bool, uint32_t, uint8_t,
                                    const char *, const uint8_t *) {
    return WIFI_SCAN_FAILED;
}
int16_t FakeWiFiClass::scanNetworks(bool, bool, bool, uint32_t, uint8_t) {
    return WIFI_SCAN_FAILED;
}
const uint8_t *FakeWiFiClass::BSSID(int16_t) const { return nullptr; }
int FakeWiFiClass::RSSI(int16_t) const { return 0; }
int FakeWiFiClass::channel(int16_t) const { return 0; }
String FakeWiFiClass::SSID(int16_t) const { return String(); }
void FakeWiFiClass::reset() {}
void FakeWiFiClass::complete(const std::vector<FakeWifiResult> &) {}
void FakeWiFiClass::fail() {}
void FakeWiFiClass::startExternalScan() {}
void FakeWiFiClass::driverStop() {}
void FakeWiFiClass::deliverStoppedScanDone() {}

struct TestTheme {
    uint32_t bg = 0x101820;
    uint32_t card = 0x182430;
    uint32_t text = 0xffffff;
    uint32_t textDim = 0xa0a0a0;
    uint32_t accent = 0x40a0ff;
} testTheme;
#define TH testTheme

static lv_display_t *lvDisp;
static lv_indev_t *lvIndev;
static lv_obj_t *mainScreen;
static lv_group_t *navGroup;

static void applyScreenStyle(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(TH.bg), 0);
}
static lv_obj_t *createHeader(lv_obj_t *screen, const char *title) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, title);
    return label;
}
static void styleListBtn(lv_obj_t *) {}
static lv_obj_t *createActionBtn(lv_obj_t *screen, const char *text,
                                 lv_event_cb_t callback, void *data = nullptr) {
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
    return button;
}
static void setGroup(lv_group_t *group) { lv_indev_set_group(lvIndev, group); }
static lv_color_t bleRssiColor(int16_t) { return lv_color_hex(TH.accent); }

enum class ChildAction { None, Save, Track, Associate };
static ChildAction childAction;
static lv_obj_t *childScreen;
static rogue_radar::KnownAddress childObservation;

static void openChild(ChildAction action,
                      const rogue_radar::KnownAddress &observation) {
    childAction = action;
    childObservation = observation;
    childScreen = lv_obj_create(nullptr);
    lv_screen_load(childScreen);
}
static void knownUiSaveCandidate(const rogue_radar::KnownAddress &observation,
                                 rogue_radar::KnownRadio) {
    openChild(ChildAction::Save, observation);
}
static void knownUiTrackCandidate(const rogue_radar::KnownAddress &observation,
                                  rogue_radar::KnownRadio) {
    openChild(ChildAction::Track, observation);
}
static void knownUiAssociateCandidate(const rogue_radar::KnownAddress &observation,
                                      rogue_radar::KnownRadio) {
    openChild(ChildAction::Associate, observation);
}
static bool knownUiIsSaved(rogue_radar::KnownRadio,
                           const rogue_radar::KnownAddress &, char *, size_t) {
    return false;
}
static bool processKnownUiSavedLookups() { return false; }

#include "../rogue-radar/nearby_signals_ui.h"

static void createSavedDevices() {}
static void processKnownDevices() {}
static bool knownDevicesActive() { return false; }
static void knownInstallSaveButton(lv_obj_t *, lv_group_t *, lv_obj_t *, lv_obj_t *,
                                   rogue_radar::KnownRadio, const char *, const char *,
                                   int8_t, uint8_t, uint32_t, uint8_t) {}

static void step(uint32_t duration) {
    fakeMillis += duration;
    lv_tick_inc(duration);
    lv_timer_handler();
}

static lv_obj_t *findButton(const char *text) {
    const uint32_t count = lv_obj_get_child_count(rogue_radar::nearby_ui::state.screen);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t *object = lv_obj_get_child(rogue_radar::nearby_ui::state.screen,
                                           static_cast<int32_t>(i));
        if (!lv_obj_check_type(object, &lv_button_class)) continue;
        lv_obj_t *label = lv_obj_get_child(object, 0);
        if (label && lv_obj_check_type(label, &lv_label_class) &&
            std::strcmp(lv_label_get_text(label), text) == 0)
            return object;
    }
    return nullptr;
}

static void click(const char *text) {
    lv_obj_t *button = findButton(text);
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

static void returnFromChild() {
    using namespace rogue_radar::nearby_ui;
    assert(childScreen && lv_screen_active() == childScreen);
    process();
    assert(state.suspended && state.childSeen);
    lv_screen_load(state.screen);
    lv_obj_delete(childScreen);
    childScreen = nullptr;
    process();
    assert(!state.suspended && !state.childSeen);
}

static void runDetailAction(const char *buttonText, ChildAction expected) {
    using namespace rogue_radar::nearby_ui;
    assert(state.view == View::Detail);
    click(buttonText);
    assert(state.pending != Pending::None);
    // The stale status regression happens synchronously in the click handler.
    // Dispatch directly after that boundary so the test can also verify the
    // child-screen lifecycle without depending on the radio driver's fence.
    dispatchPending();
    assert(childAction == expected);
    assert(std::strcmp(childObservation.address, "AA:BB:CC:DD:EE:04") == 0);
    returnFromChild();
}

int main() {
    using namespace rogue_radar;
    using namespace rogue_radar::nearby_ui;

    (void)&createSavedDevices;
    (void)&processKnownDevices;
    (void)&knownDevicesActive;
    (void)&knownInstallSaveButton;
    (void)&createNearbySignals;
    (void)&createLearnKnownDevice;
    (void)&createNearbySignalsForAssociation;
    (void)&processNearbySignals;
    (void)&nearbySignalsActive;
    (void)&nearbySignalsSuspended;

    lv_init();
    lvDisp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_default(lvDisp);
    lv_timer_pause(lv_display_get_refr_timer(lvDisp));
    lvIndev = lv_indev_create();
    lv_indev_set_type(lvIndev, LV_INDEV_TYPE_ENCODER);
    mainScreen = lv_screen_active();
    navGroup = lv_group_create();
    setGroup(navGroup);

    create(false, false, KnownRadio::Ble);
    step(300);
    assert(state.active && state.view == View::List && state.table);

    for (int i = 1; i <= 4; ++i) {
        KnownAddress observation{};
        std::snprintf(observation.address, sizeof(observation.address),
                      "AA:BB:CC:DD:EE:%02d", i);
        std::snprintf(observation.advertisedName,
                      sizeof(observation.advertisedName), "Device %d", i);
        observation.addressType = 1;
        observation.lastRssi = static_cast<int8_t>(-40 - i);
        assert(state.table->observe(KnownRadio::Ble, observation, fakeMillis) ==
               KnownSignalObserveResult::Added);
    }
    state.table->refreshRanking(fakeMillis, 0);
    paintList();

    click(">");
    assert(state.page == 1);
    paintList();
    lv_obj_send_event(state.rows[0], LV_EVENT_CLICKED, nullptr);
    process();
    assert(state.view == View::Detail);
    assert(std::strcmp(state.selectedObservation.address,
                       "AA:BB:CC:DD:EE:04") == 0);

    runDetailAction("Save", ChildAction::Save);
    runDetailAction("Track", ChildAction::Track);
    runDetailAction("Associate", ChildAction::Associate);

    click("Back");
    process();
    assert(state.view == View::List && state.page == 1);

    lv_mem_monitor_t memory{};
    lv_mem_monitor(&memory);
    assert(memory.free_size > 8192);
    std::printf("PASS Nearby page 2 Save/Track/Associate and child returns (%u bytes free)\n",
                static_cast<unsigned>(memory.free_size));
}

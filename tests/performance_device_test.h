#pragma once

// Physical-board production-path regression for long-running UI churn. This
// diagnostic synthesizes observations only; it never starts a radio scan or
// changes persistent preferences. It is absent unless the macro is enabled.
#if defined(ROGUE_RADAR_PERFORMANCE_TEST) && ROGUE_RADAR_PERFORMANCE_TEST

namespace performance_device_test {

enum class Stage : uint8_t {
    WaitBoot, WaitBleMenu, WaitBleScreen, BleSeed, BleStress,
    WaitBleMenuReturn, WaitMainAfterBle, WaitWifiMenu, WaitFlockScreen,
    FlockSeed, FlockStress, WaitWifiMenuAfterFlock, WaitHybridScreen,
    HybridSeed, HybridStress, WaitWifiMenuAfterHybrid, WaitMainDone,
    Done, Failed
};

static constexpr uint8_t kCapacity = MAX_BLE_RESULTS;
static constexpr uint8_t kProtectedIndex = 7;
static constexpr uint16_t kStressObservations = 600;
static constexpr uint8_t kBatchSize = 8;
static constexpr uint16_t kFlockStressObservations = 600;
static constexpr uint16_t kHybridStressObservations = 600;

static Stage stage = Stage::WaitBoot;
static uint32_t stageAt = 0;
static uint16_t injected = 0;
static lv_obj_t *rows[kCapacity]{};
static lv_group_t *stableGroup = nullptr;
static lv_obj_t *focusedRow = nullptr;
static char focusedMac[18]{};
static uint8_t focusedAddressType = 0;
static uint32_t baselineHeap = 0;
static uint32_t baselineLvglFree = 0;
static uint32_t minimumHeap = UINT32_MAX;
static uint32_t minimumLvglFree = UINT32_MAX;
static uint32_t syntheticSeenBase = 0;
static lv_obj_t *flockStableRows[MAX_FLOCK_HITS]{};
static lv_group_t *flockStableGroup = nullptr;
static lv_obj_t *flockFocusedControl = nullptr;
static lv_obj_t *hybridStableRows[MAX_FLOCK_HYBRID_HITS]{};
static lv_group_t *hybridStableGroup = nullptr;
static lv_obj_t *hybridFocusedRow = nullptr;
static char hybridFocusedMac[18]{};

static void memory(uint32_t &heap, uint32_t &lvglFree, uint32_t &lvglBiggest) {
    lv_mem_monitor_t monitor{};
    lv_mem_monitor(&monitor);
    heap = ESP.getFreeHeap();
    lvglFree = monitor.free_size;
    lvglBiggest = monitor.free_biggest_size;
    if (heap < minimumHeap) minimumHeap = heap;
    if (lvglFree < minimumLvglFree) minimumLvglFree = lvglFree;
}

static void fail(const char *reason) {
    Serial.printf("[PerfDiag] ASSERTFAIL stage=%u injected=%u: %s\n",
                  static_cast<unsigned>(stage), static_cast<unsigned>(injected), reason);
    stage = Stage::Failed;
}

static LiveBleObservation observationFor(uint16_t sequence) {
    LiveBleObservation observation{};
    observation.mode = LiveBleMode::Generic;
    observation.type = BLE_GENERIC;
    observation.addressType = 1;
    observation.rssi = static_cast<int8_t>(-35 - (sequence % 55));
    observation.seenMs = syntheticSeenBase + sequence + 1;
    observation.hasName = true;
    snprintf(observation.name, sizeof(observation.name), "Synthetic-%03u",
             static_cast<unsigned>(sequence % 1000));
    snprintf(observation.mac, sizeof(observation.mac), "02:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned>((sequence >> 8) & 0xff),
             static_cast<unsigned>(sequence & 0xff),
             static_cast<unsigned>((sequence * 3U) & 0xff),
             static_cast<unsigned>((sequence * 5U) & 0xff),
             static_cast<unsigned>((sequence * 7U) & 0xff));
    return observation;
}

static void restoreIdleSession() {
    // pollLiveBleUi deliberately traverses the production poll path. No radio
    // is running in this synthetic test, so discard only that expected error.
    scanSession.error[0] = '\0';
    scanSession.state = ScanUiState::Idle;
}

static bool verifyStableUi() {
    if (bleEntryCount != kCapacity) { fail("BLE result count changed"); return false; }
    if (lv_obj_get_child_count(bleScanList) != kCapacity) {
        fail("BLE row count changed"); return false;
    }
    if (bleToolGroup != stableGroup) { fail("focus group was rebuilt"); return false; }
    if (lv_group_get_focused(bleToolGroup) != focusedRow) {
        fail("focused row changed"); return false;
    }
    if (strcmp(bleEntries[kProtectedIndex].mac, focusedMac) != 0 ||
        bleEntries[kProtectedIndex].addressType != focusedAddressType) {
        fail("focused identity was evicted"); return false;
    }
    for (uint8_t i = 0; i < kCapacity; ++i) {
        if (lv_obj_get_child(bleScanList, i) != rows[i]) {
            fail("BLE row object was recreated"); return false;
        }
        if (lv_obj_get_group(rows[i]) != stableGroup) {
            fail("BLE row left stable group"); return false;
        }
    }
    return true;
}

static void seed() {
    syntheticSeenBase = millis() - 1000;
    resetLiveBleResults(LiveBleMode::Generic);
    liveBleMode = LiveBleMode::Generic;
    liveBleProtectedIndex = -1;
    liveBleResultsDirty = false;
    for (uint8_t i = 0; i < kCapacity; ++i) {
        const LiveBleObservation observation = observationFor(i);
        if (!applyLiveBleObservation(observation)) {
            fail("initial observation did not grow the list"); return;
        }
        liveBleResultsDirty = true;
    }
    liveBleLastUiRefreshMs = millis() - 1000;
    pollLiveBleUi();
    restoreIdleSession();
    if (lv_obj_get_child_count(bleScanList) != kCapacity) {
        fail("initial production rebuild did not create full list"); return;
    }
    stableGroup = bleToolGroup;
    for (uint8_t i = 0; i < kCapacity; ++i) rows[i] = lv_obj_get_child(bleScanList, i);
    focusedRow = rows[kProtectedIndex];
    lv_group_focus_obj(focusedRow);
    snprintf(focusedMac, sizeof(focusedMac), "%s", bleEntries[kProtectedIndex].mac);
    focusedAddressType = bleEntries[kProtectedIndex].addressType;

    // One production poll snapshots the focused result as the eviction guard.
    liveBleLastUiRefreshMs = millis() - 1000;
    pollLiveBleUi();
    restoreIdleSession();
    if (liveBleProtectedIndex != kProtectedIndex || !verifyStableUi()) return;

    uint32_t biggest = 0;
    memory(baselineHeap, baselineLvglFree, biggest);
    Serial.printf("[PerfDiag] seeded rows=%u heap=%lu lvgl=%lu largest=%lu focus=%s\n",
                  static_cast<unsigned>(kCapacity), (unsigned long)baselineHeap,
                  (unsigned long)baselineLvglFree, (unsigned long)biggest, focusedMac);
    stage = Stage::BleStress;
}

static void stressBatch() {
    const uint16_t remaining = kStressObservations - injected;
    const uint8_t batch = remaining < kBatchSize ? static_cast<uint8_t>(remaining) : kBatchSize;
    for (uint8_t i = 0; i < batch; ++i) {
        const LiveBleObservation observation = observationFor(kCapacity + injected++);
        if (applyLiveBleObservation(observation)) {
            fail("full-capacity replacement reported structural growth"); return;
        }
    }

    // Force the production one-second repaint gate without waiting ten minutes
    // for 600 observations. A correct replacement updates existing rows only.
    liveBleLastUiRefreshMs = millis() - 1000;
    pollLiveBleUi();
    restoreIdleSession();
    if (!verifyStableUi()) return;

    uint32_t heap = 0, lvglFree = 0, biggest = 0;
    memory(heap, lvglFree, biggest);
    if ((injected % 80U) == 0U || injected == kStressObservations)
        Serial.printf("[PerfDiag] progress=%u heap=%lu lvgl=%lu largest=%lu\n",
                      static_cast<unsigned>(injected), (unsigned long)heap,
                      (unsigned long)lvglFree, (unsigned long)biggest);

    if (injected != kStressObservations) return;
    // Permit a small allocator bookkeeping delta, but reject accumulating row
    // or label allocations. LVGL should finish with the same available pool.
    if (lvglFree < baselineLvglFree) { fail("LVGL pool declined during stable repaint"); return; }
    if (heap + 512U < baselineHeap) { fail("heap declined during stable repaint"); return; }
    Serial.printf("[PerfDiag] PASS BLE capacity churn observations=%u heap=%lu(min=%lu) lvgl=%lu(min=%lu)\n",
                  static_cast<unsigned>(injected), (unsigned long)heap,
                  (unsigned long)minimumHeap, (unsigned long)lvglFree,
                  (unsigned long)minimumLvglFree);
    cb_bleToolBack(nullptr);
    stage = Stage::WaitBleMenuReturn;
    stageAt = millis();
}

static rogue_radar::FlockObservation flockObservationFor(uint16_t sequence) {
    rogue_radar::FlockObservation observation{};
    snprintf(observation.name, sizeof(observation.name), "Flock-%03u",
             static_cast<unsigned>(sequence % 1000));
    snprintf(observation.mac, sizeof(observation.mac), "06:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned>((sequence >> 8) & 0xff),
             static_cast<unsigned>(sequence & 0xff),
             static_cast<unsigned>((sequence * 3U) & 0xff),
             static_cast<unsigned>((sequence * 5U) & 0xff),
             static_cast<unsigned>((sequence * 7U) & 0xff));
    snprintf(observation.method, sizeof(observation.method), "Synthetic queue");
    snprintf(observation.confidence, sizeof(observation.confidence), "Hint");
    snprintf(observation.type, sizeof(observation.type), "Flock candidate");
    observation.rssi = static_cast<int8_t>(-35 - (sequence % 55));
    observation.seenMs = syntheticSeenBase + sequence + 1;
    return observation;
}

static bool verifyFlockStableUi() {
    if (flockHitCount != MAX_FLOCK_HITS || flockRowCount != MAX_FLOCK_HITS ||
        lv_obj_get_child_count(flockList) != MAX_FLOCK_HITS) {
        fail("Flock result/row count changed"); return false;
    }
    if (wifiToolGroup != flockStableGroup) { fail("Flock group was rebuilt"); return false; }
    if (lv_group_get_focused(wifiToolGroup) != flockFocusedControl) {
        fail("Flock control focus changed"); return false;
    }
    for (uint8_t i = 0; i < MAX_FLOCK_HITS; ++i) {
        if (flockRows[i] != flockStableRows[i] ||
            lv_obj_get_child(flockList, i) != flockStableRows[i]) {
            fail("Flock row object was recreated"); return false;
        }
    }
    return true;
}

static void seedFlock() {
    syntheticSeenBase = millis() - 1000;
    for (uint8_t first = 0; first < MAX_FLOCK_HITS; first += kBatchSize) {
        const uint8_t candidateEnd = first + kBatchSize;
        const uint8_t end = candidateEnd < MAX_FLOCK_HITS ? candidateEnd : MAX_FLOCK_HITS;
        for (uint8_t i = first; i < end; ++i)
            pushFlockQueue(flockQueue, flockObservationFor(i));
        flockLastPaint = millis() - 500;
        flock_refresh_cb(nullptr);
    }
    if (flockHitCount != MAX_FLOCK_HITS || flockRowCount != MAX_FLOCK_HITS) {
        fail("Flock seed did not reach capacity"); return;
    }
    // A reply addressed to a candidate does not refresh that candidate's
    // direct RSSI age. Exercise the production merge, not a model copy.
    const uint32_t directSeen = flockHits[0].lastSeen;
    const int8_t directRssi = flockHits[0].rssi;
    auto receiver = flockObservationFor(0);
    receiver.receiverOnly = true; receiver.rssi = -127;
    receiver.seenMs = directSeen + 1000;
    pushFlockQueue(flockQueue, receiver);
    flock_refresh_cb(nullptr);
    if (flockHits[0].receiverOnly || flockHits[0].lastSeen != directSeen ||
        flockHits[0].rssi != directRssi) { fail("receiver hint refreshed direct WiFi signal"); return; }
    flockStableGroup = wifiToolGroup;
    flockFocusedControl = lv_group_get_focused(wifiToolGroup);
    for (uint8_t i = 0; i < MAX_FLOCK_HITS; ++i) flockStableRows[i] = flockRows[i];
    if (!verifyFlockStableUi()) return;
    injected = 0;
    minimumHeap = minimumLvglFree = UINT32_MAX;
    uint32_t biggest = 0;
    memory(baselineHeap, baselineLvglFree, biggest);
    Serial.printf("[PerfDiag] Flock seeded rows=%u heap=%lu lvgl=%lu largest=%lu\n",
                  unsigned(MAX_FLOCK_HITS), (unsigned long)baselineHeap,
                  (unsigned long)baselineLvglFree, (unsigned long)biggest);
    stage = Stage::FlockStress;
}

static void stressFlockBatch() {
    const uint16_t remaining = kFlockStressObservations - injected;
    const uint8_t batch = remaining < kBatchSize ? uint8_t(remaining) : kBatchSize;
    for (uint8_t i = 0; i < batch; ++i)
        pushFlockQueue(flockQueue, flockObservationFor(MAX_FLOCK_HITS + injected++));
    flockLastPaint = millis() - 500;
    flock_refresh_cb(nullptr);
    if (!verifyFlockStableUi()) return;
    uint32_t heap = 0, lvglFree = 0, biggest = 0;
    memory(heap, lvglFree, biggest);
    if ((injected % 80U) == 0U || injected == kFlockStressObservations)
        Serial.printf("[PerfDiag] Flock progress=%u heap=%lu lvgl=%lu largest=%lu drops=%lu\n",
                      unsigned(injected), (unsigned long)heap, (unsigned long)lvglFree,
                      (unsigned long)biggest, (unsigned long)flockQueue.drops());
    if (injected != kFlockStressObservations) return;
    if (lvglFree < baselineLvglFree) { fail("Flock LVGL pool declined"); return; }
    if (heap + 512U < baselineHeap) { fail("Flock heap declined"); return; }
    Serial.println("[PerfDiag] PASS Flock capacity churn");
    cb_wifiToolBack(nullptr);
    stage = Stage::WaitWifiMenuAfterFlock;
    stageAt = millis();
}

static bool verifyHybridStableUi() {
    if (hybridHitCount != MAX_FLOCK_HYBRID_HITS ||
        hybridRowCount != MAX_FLOCK_HYBRID_HITS ||
        lv_obj_get_child_count(hybridList) != MAX_FLOCK_HYBRID_HITS) {
        fail("Hybrid result/row count changed"); return false;
    }
    if (wifiToolGroup != hybridStableGroup) { fail("Hybrid group was rebuilt"); return false; }
    if (hybridFocusedIndex() != kProtectedIndex ||
        lv_group_get_focused(wifiToolGroup) != hybridFocusedRow) {
        fail("Hybrid focus changed"); return false;
    }
    if (strcmp(hybridHits[kProtectedIndex].mac, hybridFocusedMac)) {
        fail("Hybrid focused identity was evicted"); return false;
    }
    for (uint8_t i = 0; i < MAX_FLOCK_HYBRID_HITS; ++i) {
        if (hybridRows[i] != hybridStableRows[i] ||
            lv_obj_get_child(hybridList, i) != hybridStableRows[i]) {
            fail("Hybrid row object was recreated"); return false;
        }
    }
    return true;
}

static void addHybrid(uint16_t sequence) {
    const auto observation = flockObservationFor(sequence);
    hybridUpsertHit("WiFi", observation.name, observation.mac, observation.rssi,
                    "Synthetic queue", observation.method, observation.confidence,
                    observation.type, false, observation.count);
}

static void seedHybrid() {
    syntheticSeenBase = millis() - 1000;
    for (uint8_t i = 0; i < MAX_FLOCK_HYBRID_HITS; ++i) addHybrid(i);
    hybridLastPaint = millis() - 500;
    hybridRebuildList();
    if (hybridHitCount != MAX_FLOCK_HYBRID_HITS ||
        hybridRowCount != MAX_FLOCK_HYBRID_HITS) {
        fail("Hybrid seed did not reach capacity"); return;
    }
    hybridHits[0].lastSeen = millis() - 1000;
    const uint32_t directSeen = hybridHits[0].lastSeen;
    const int8_t directRssi = hybridHits[0].rssi;
    const auto receiver = flockObservationFor(0);
    hybridUpsertHit("WiFi", receiver.name, receiver.mac, -127, "Receiver hint",
                    receiver.method, "Hint", receiver.type, true);
    if (hybridHits[0].receiverOnly || hybridHits[0].lastSeen != directSeen ||
        hybridHits[0].rssi != directRssi) { fail("receiver hint refreshed direct Hybrid signal"); return; }
    hybridStableGroup = wifiToolGroup;
    for (uint8_t i = 0; i < MAX_FLOCK_HYBRID_HITS; ++i)
        hybridStableRows[i] = hybridRows[i];
    hybridFocusedRow = hybridRows[kProtectedIndex];
    lv_group_focus_obj(hybridFocusedRow);
    snprintf(hybridFocusedMac, sizeof(hybridFocusedMac), "%s",
             hybridHits[kProtectedIndex].mac);
    if (!verifyHybridStableUi()) return;
    injected = 0;
    minimumHeap = minimumLvglFree = UINT32_MAX;
    uint32_t biggest = 0;
    memory(baselineHeap, baselineLvglFree, biggest);
    Serial.printf("[PerfDiag] Hybrid seeded rows=%u heap=%lu lvgl=%lu largest=%lu focus=%s\n",
                  unsigned(MAX_FLOCK_HYBRID_HITS), (unsigned long)baselineHeap,
                  (unsigned long)baselineLvglFree, (unsigned long)biggest,
                  hybridFocusedMac);
    stage = Stage::HybridStress;
}

static void stressHybridBatch() {
    const uint16_t remaining = kHybridStressObservations - injected;
    const uint8_t batch = remaining < kBatchSize ? uint8_t(remaining) : kBatchSize;
    for (uint8_t i = 0; i < batch; ++i)
        addHybrid(MAX_FLOCK_HYBRID_HITS + injected++);
    hybridLastPaint = millis() - 500;
    hybridRebuildList();
    if (!verifyHybridStableUi()) return;
    uint32_t heap = 0, lvglFree = 0, biggest = 0;
    memory(heap, lvglFree, biggest);
    if ((injected % 80U) == 0U || injected == kHybridStressObservations)
        Serial.printf("[PerfDiag] Hybrid progress=%u heap=%lu lvgl=%lu largest=%lu\n",
                      unsigned(injected), (unsigned long)heap, (unsigned long)lvglFree,
                      (unsigned long)biggest);
    if (injected != kHybridStressObservations) return;
    if (lvglFree < baselineLvglFree) { fail("Hybrid LVGL pool declined"); return; }
    if (heap + 512U < baselineHeap) { fail("Hybrid heap declined"); return; }
    Serial.println("[PerfDiag] PASS Hybrid capacity churn");
    cb_wifiToolBack(nullptr);
    stage = Stage::WaitWifiMenuAfterHybrid;
    stageAt = millis();
}

static void begin() {
    Serial.println("[PerfDiag] BEGIN production BLE/Flock/Hybrid capacity churn");
    stage = Stage::WaitBoot;
    stageAt = millis() + 1500;
}

static void process() {
    if (stage == Stage::Done || stage == Stage::Failed) return;
    if (stage == Stage::WaitBoot) {
        if (static_cast<int32_t>(millis() - stageAt) < 0 || lv_display_get_screen_prev(lvDisp)) return;
        createBLEMenu();
        stage = Stage::WaitBleMenu;
        stageAt = millis() + 350;
        return;
    }
    if (stage == Stage::WaitBleMenu) {
        if (static_cast<int32_t>(millis() - stageAt) < 0 ||
            lv_display_get_screen_prev(lvDisp) || lv_screen_active() != bleMenuScreen) return;
        createBLEScanner();
        stage = Stage::WaitBleScreen;
        stageAt = millis() + 350;
        return;
    }
    if (stage == Stage::WaitBleScreen) {
        if (static_cast<int32_t>(millis() - stageAt) < 0 ||
            lv_display_get_screen_prev(lvDisp) || lv_screen_active() != bleToolScreen) return;
        stage = Stage::BleSeed;
    }
    if (stage == Stage::BleSeed) { seed(); return; }
    if (stage == Stage::BleStress) { stressBatch(); return; }
    if (stage == Stage::WaitBleMenuReturn) {
        if (millis() - stageAt > 5000) { fail("BLE Back did not reach BLE menu"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != bleMenuScreen) return;
        cb_bleMenuBack(nullptr);
        stage = Stage::WaitMainAfterBle;
        stageAt = millis();
        return;
    }
    if (stage == Stage::WaitMainAfterBle) {
        if (millis() - stageAt > 5000) { fail("BLE menu Back did not reach main"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != mainScreen) return;
        createWiFiMenu();
        stage = Stage::WaitWifiMenu;
        stageAt = millis();
        return;
    }
    if (stage == Stage::WaitWifiMenu) {
        if (millis() - stageAt > 5000) { fail("WiFi menu did not open"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != wifiMenuScreen) return;
        createFlockDetector();
        stage = Stage::WaitFlockScreen;
        stageAt = millis();
        return;
    }
    if (stage == Stage::WaitFlockScreen) {
        if (millis() - stageAt > 5000) { fail("Flock screen did not open"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != wifiToolScreen) return;
        stage = Stage::FlockSeed;
    }
    if (stage == Stage::FlockSeed) { seedFlock(); return; }
    if (stage == Stage::FlockStress) { stressFlockBatch(); return; }
    if (stage == Stage::WaitWifiMenuAfterFlock) {
        if (millis() - stageAt > 5000) { fail("Flock Back did not reach WiFi menu"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != wifiMenuScreen) return;
        createFlockHybridScanner();
        stage = Stage::WaitHybridScreen;
        stageAt = millis();
        return;
    }
    if (stage == Stage::WaitHybridScreen) {
        if (millis() - stageAt > 5000) { fail("Hybrid screen did not open"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != wifiToolScreen) return;
        stage = Stage::HybridSeed;
    }
    if (stage == Stage::HybridSeed) { seedHybrid(); return; }
    if (stage == Stage::HybridStress) { stressHybridBatch(); return; }
    if (stage == Stage::WaitWifiMenuAfterHybrid) {
        if (millis() - stageAt > 5000) { fail("Hybrid Back did not reach WiFi menu"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != wifiMenuScreen) return;
        cb_wifiMenuBack(nullptr);
        stage = Stage::WaitMainDone;
        stageAt = millis();
        return;
    }
    if (stage == Stage::WaitMainDone) {
        if (millis() - stageAt > 5000) { fail("WiFi menu Back did not reach main"); return; }
        if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != mainScreen) return;
        Serial.println("[PerfDiag] PASS production performance lifecycle complete");
        stage = Stage::Done;
        return;
    }
}

}  // namespace performance_device_test

static void performanceDeviceTestBegin() { performance_device_test::begin(); }
static void performanceDeviceTestProcess() { performance_device_test::process(); }

#endif  // ROGUE_RADAR_PERFORMANCE_TEST

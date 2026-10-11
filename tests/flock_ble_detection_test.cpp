#include <cassert>
#include <cstring>
#include <iostream>

#include "../rogue-radar/flock_ble_detection.h"

using namespace rogue_radar;

static const uint8_t ordinaryMac[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
static const uint8_t candidateMac[6] = {0xe4,0xaa,0xea,0x40,0x50,0x60};

static FlockBleObservation observation(const char *name = nullptr) {
    FlockBleObservation value{};
    value.name = name;
    value.mac = ordinaryMac;
    value.addressType = 1;
    return value;
}

static void testStrongAccessoryUuid() {
    const char *services[] = {"e8ccbb38-9532-46a8-9fe5-1814df172e6f"};
    auto value = observation("Android");
    value.serviceUuids = services;
    value.serviceUuidCount = 1;
    FlockBleClassification out{};
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::FlockAccessoryService);
    assert(out.confidence == FlockWifiConfidence::High);
    assert(out.deviceType == FlockBleDeviceType::FlockAccessory);
}

static void testNames() {
    FlockBleClassification out{};
    auto penguin = observation("Penguin-1234567890");
    assert(classifyFlockBle(penguin, out));
    assert(out.evidence == FlockBleEvidence::PenguinSerialName);
    assert(out.confidence == FlockWifiConfidence::Medium);

    auto bare = observation("1234567890");
    assert(classifyFlockBle(bare, out));
    assert(out.evidence == FlockBleEvidence::BareSerialName);
    assert(out.confidence == FlockWifiConfidence::Hint);

    auto fs = observation("FS Ext Battery");
    assert(classifyFlockBle(fs, out));
    assert(out.evidence == FlockBleEvidence::ExternalBatteryName);
    assert(out.confidence == FlockWifiConfidence::Medium);

    auto malformed = observation("Penguin-123456789");
    assert(!classifyFlockBle(malformed, out));
}

static void testGenericNamesDoNotMatch() {
    FlockBleClassification out{};
    assert(!classifyFlockBle(observation("Android"), out));
    assert(!classifyFlockBle(observation("msm8953_32"), out));
    assert(!classifyFlockBle(observation("Penguin toy"), out));
}

static void testExplicitLegacyFlockNameIsOnlyAHint() {
    FlockBleClassification out{};
    auto value = observation("Flock-ABCDEF");
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::FlockNameHint);
    assert(out.confidence == FlockWifiConfidence::Hint);

    value = observation("FlockOS accessory");
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::FlockNameHint);
    assert(out.confidence == FlockWifiConfidence::Hint);
}

static void testManufacturerBoundsAndCorroboration() {
    FlockBleClassification out{};
    uint8_t truncated[] = {0xc8};
    auto value = observation();
    value.manufacturerData = truncated;
    value.manufacturerDataLength = sizeof(truncated);
    assert(!classifyFlockBle(value, out));

    uint8_t companyOnly[] = {0xc8,0x09,0x01};
    value.manufacturerData = companyOnly;
    value.manufacturerDataLength = sizeof(companyOnly);
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::XuntongCompanyId);
    assert(out.confidence == FlockWifiConfidence::Hint);

    uint8_t serial[] = {0xc8,0x09,'T','N','7','2','0','2','3','0','2','2','0','0','0','7','7','1'};
    value.manufacturerData = serial;
    value.manufacturerDataLength = sizeof(serial);
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::XuntongSerialPayload);
    assert(out.confidence == FlockWifiConfidence::Medium);
}

static void testGenericDfuAndRavenRemainHints() {
    FlockBleClassification out{};
    const char *dfu[] = {"00001530-1212-efde-1523-785feabcd123"};
    auto value = observation("DfuTarg");
    value.serviceUuids = dfu;
    value.serviceUuidCount = 1;
    assert(classifyFlockBle(value, out));
    assert(out.confidence == FlockWifiConfidence::Hint);

    const char *raven[] = {"3101"};
    value = observation();
    value.serviceUuids = raven;
    value.serviceUuidCount = 1;
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::RavenServiceRange);
    assert(out.confidence == FlockWifiConfidence::Hint);
}

static void testOuiIsOnlyHintAndDoesNotOverrideStrongerEvidence() {
    FlockBleClassification out{};
    auto value = observation();
    value.mac = candidateMac;
    value.addressType = 0;
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::CuratedOui);
    assert(out.confidence == FlockWifiConfidence::Hint);

    value.name = "Penguin-1234567890";
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::PenguinSerialName);
    assert(out.confidence == FlockWifiConfidence::Medium);

    value = observation();
    value.mac = candidateMac;
    value.addressType = 1;
    assert(!classifyFlockBle(value, out));

    value.addressType = 2;
    assert(classifyFlockBle(value, out));
    assert(out.evidence == FlockBleEvidence::CuratedOui);
}

int main() {
    testStrongAccessoryUuid();
    testNames();
    testGenericNamesDoNotMatch();
    testExplicitLegacyFlockNameIsOnlyAHint();
    testManufacturerBoundsAndCorroboration();
    testGenericDfuAndRavenRemainHints();
    testOuiIsOnlyHintAndDoesNotOverrideStrongerEvidence();
    std::cout << "flock_ble_detection_test: PASS\n";
}

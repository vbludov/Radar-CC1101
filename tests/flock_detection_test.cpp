#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../rogue-radar/flock_detection.h"

using namespace rogue_radar;

static std::vector<uint8_t> frame(uint8_t type, uint8_t subtype,
                                  const uint8_t rx[6], const uint8_t tx[6]) {
    std::vector<uint8_t> value(24, 0);
    value[0] = uint8_t((type << 2) | (subtype << 4));
    std::memcpy(&value[4], rx, 6);
    std::memcpy(&value[10], tx, 6);
    return value;
}

static void ie(std::vector<uint8_t> &value, uint8_t id,
               std::initializer_list<uint8_t> body) {
    value.push_back(id);
    value.push_back(uint8_t(body.size()));
    value.insert(value.end(), body.begin(), body.end());
}

static void ssid(std::vector<uint8_t> &value, const char *name) {
    value.push_back(0);
    value.push_back(uint8_t(std::strlen(name)));
    value.insert(value.end(), name, name + std::strlen(name));
}

static void fixedParameters(std::vector<uint8_t> &value) {
    value.resize(36, 0);
}

static const uint8_t broadcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
static const uint8_t flockOui[6] = {0xe4,0xaa,0xea,0x80,0xa1,0x9b};
static const uint8_t removedOui[6] = {0xf8,0xa2,0xd6,0x01,0x02,0x03};
static const uint8_t ordinary[6] = {0x10,0x20,0x30,0x40,0x50,0x60};

static void testBoundsAndRemovedOuis() {
    FlockWifiClassification out{};
    uint8_t shortFrame[23]{};
    assert(!classifyFlockWifiFrame(shortFrame, sizeof(shortFrame), -40, 1, out));
    auto value = frame(0, 4, broadcast, removedOui);
    ie(value, 0, {});
    assert(!classifyFlockWifiFrame(value.data(), value.size(), -50, 6, out));
}

static void testWildcardProbeUsesTransmitter() {
    FlockWifiClassification out{};
    auto value = frame(0, 4, broadcast, flockOui);
    ie(value, 0, {});
    assert(classifyFlockWifiFrame(value.data(), value.size(), -71, 7, out));
    assert(out.count == 1);
    assert(out.hits[0].role == FlockWifiRole::Transmitter);
    assert(out.hits[0].evidence == FlockWifiEvidence::WildcardProbeFromCuratedOui);
    assert(out.hits[0].confidence == FlockWifiConfidence::Medium);
    assert(out.hits[0].rssiAppliesToDevice);
}

static void testDirectedProbeIsOnlyOuiHint() {
    FlockWifiClassification out{};
    auto value = frame(0, 4, broadcast, flockOui);
    ssid(value, "uplink");
    assert(classifyFlockWifiFrame(value.data(), value.size(), -60, 1, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::CuratedOui);
    assert(out.hits[0].confidence == FlockWifiConfidence::Hint);

    auto phone = frame(0, 4, broadcast, ordinary);
    ssid(phone, "Flock-A1b2C3");
    assert(!classifyFlockWifiFrame(phone.data(), phone.size(), -35, 1, out));
}

static void testHiddenBeaconIsNotWildcardProbe() {
    FlockWifiClassification out{};
    auto value = frame(0, 8, broadcast, flockOui);
    fixedParameters(value);
    ie(value, 0, {});
    assert(classifyFlockWifiFrame(value.data(), value.size(), -54, 11, out));
    assert(out.hits[0].frame == FlockWifiFrame::Beacon);
    assert(out.hits[0].evidence == FlockWifiEvidence::CuratedOui);
    assert(out.hits[0].confidence == FlockWifiConfidence::Hint);
}

static void testStrictSsidAndMalformedTail() {
    FlockWifiClassification out{};
    auto value = frame(0, 8, broadcast, ordinary);
    fixedParameters(value);
    ssid(value, "Flock-A1b2C3");
    assert(classifyFlockWifiFrame(value.data(), value.size(), -48, 6, out));
    assert(!out.malformedInformationElements);
    assert(out.hits[0].evidence == FlockWifiEvidence::FlockSsidPattern);
    assert(out.hits[0].confidence == FlockWifiConfidence::High);

    value.push_back(221); value.push_back(9); value.push_back(1);
    assert(!classifyFlockWifiFrame(value.data(), value.size(), -48, 6, out));
    assert(out.malformedInformationElements);

    auto oversized = frame(0, 8, broadcast, ordinary);
    fixedParameters(oversized);
    oversized.push_back(0); oversized.push_back(33);
    oversized.insert(oversized.end(), 33, 'A');
    assert(!classifyFlockWifiFrame(oversized.data(), oversized.size(), -48, 6, out));
    assert(out.malformedInformationElements);
}

static void testExactIdentityIsNotDowngradedByName() {
    FlockWifiClassification out{};
    const uint8_t exact[6] = {0x00,0x03,0x7f,0x50,0x00,0x01};
    auto value = frame(0, 8, broadcast, exact);
    fixedParameters(value);
    ssid(value, "flock-ish");
    assert(classifyFlockWifiFrame(value.data(), value.size(), -40, 1, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::ExactFirmwareMac);
    assert(out.hits[0].confidence == FlockWifiConfidence::High);
}

static void testPrimaryFingerprint() {
    FlockWifiClassification out{};
    auto value = frame(0, 4, broadcast, flockOui);
    ie(value, 0, {});
    ie(value, 2, {1});
    ie(value, 12, {1});
    ie(value, 127, {1});
    ie(value, 221, {0x50,0x6f,0x9a,0x16,0x03,0x01,0x03});
    ie(value, 45, {1});
    ie(value, 191, {1});
    ie(value, 221, {0x00,0x50,0xf2,0x08,0x00,0x00,0x00});
    assert(classifyFlockWifiFrame(value.data(), value.size(), -66, 3, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::PrimaryProbeFingerprint);
    assert(out.hits[0].confidence == FlockWifiConfidence::High);
}

static void testReceiverEvidenceDoesNotOwnRssi() {
    FlockWifiClassification out{};
    auto value = frame(0, 5, flockOui, ordinary);
    fixedParameters(value);
    ssid(value, "ordinary");
    assert(classifyFlockWifiFrame(value.data(), value.size(), -38, 9, out));
    assert(out.count == 1);
    assert(out.hits[0].role == FlockWifiRole::Receiver);
    assert(!out.hits[0].rssiAppliesToDevice);
    assert(out.hits[0].confidence == FlockWifiConfidence::Hint);
}

static void testDataFrameCanReportBothRoles() {
    FlockWifiClassification out{};
    const uint8_t otherFlock[6] = {0x90,0x35,0xea,0x01,0x02,0x03};
    auto value = frame(2, 0, otherFlock, flockOui);
    assert(classifyFlockWifiFrame(value.data(), value.size(), -44, 4, out));
    assert(out.count == 2);
    assert(out.hits[0].role == FlockWifiRole::Transmitter);
    assert(out.hits[0].rssiAppliesToDevice);
    assert(out.hits[1].role == FlockWifiRole::Receiver);
    assert(!out.hits[1].rssiAppliesToDevice);
    assert(out.hits[0].confidence != FlockWifiConfidence::High);
    assert(out.hits[1].confidence != FlockWifiConfidence::High);
}

static void testExactFirmwareMacIsStrongOnlyAsTransmitter() {
    FlockWifiClassification out{};
    const uint8_t exact[6] = {0x00,0x03,0x7f,0x50,0x00,0x01};
    auto value = frame(2, 0, broadcast, exact);
    assert(classifyFlockWifiFrame(value.data(), value.size(), -70, 2, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::ExactFirmwareMac);
    assert(out.hits[0].confidence == FlockWifiConfidence::High);
}

static void testLegacyWifiNamesAreHintsOnly() {
    FlockWifiClassification out{};
    auto penguin = frame(0, 8, broadcast, ordinary);
    fixedParameters(penguin);
    ssid(penguin, "Penguin legacy");
    assert(classifyFlockWifiFrame(penguin.data(), penguin.size(), -52, 6, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::FlockNameHint);
    assert(out.hits[0].confidence == FlockWifiConfidence::Hint);

    auto pigvision = frame(0, 5, broadcast, ordinary);
    fixedParameters(pigvision);
    ssid(pigvision, "pigvision-camera");
    assert(classifyFlockWifiFrame(pigvision.data(), pigvision.size(), -52, 6, out));
    assert(out.hits[0].evidence == FlockWifiEvidence::FlockNameHint);
    assert(out.hits[0].confidence == FlockWifiConfidence::Hint);

    auto directed = frame(0, 4, broadcast, ordinary);
    ssid(directed, "Penguin legacy");
    assert(!classifyFlockWifiFrame(directed.data(), directed.size(), -30, 1, out));
}

int main() {
    testBoundsAndRemovedOuis();
    testWildcardProbeUsesTransmitter();
    testDirectedProbeIsOnlyOuiHint();
    testHiddenBeaconIsNotWildcardProbe();
    testStrictSsidAndMalformedTail();
    testPrimaryFingerprint();
    testReceiverEvidenceDoesNotOwnRssi();
    testDataFrameCanReportBothRoles();
    testExactFirmwareMacIsStrongOnlyAsTransmitter();
    testLegacyWifiNamesAreHintsOnly();
    testExactIdentityIsNotDowngradedByName();
    assert(std::strcmp(flockWifiRoleLabel(FlockWifiRole::Receiver), "Receiver") == 0);
    assert(std::strcmp(flockWifiConfidenceLabel(FlockWifiConfidence::Hint), "Hint") == 0);
    assert(flockCandidateOui(flockOui));
    assert(!flockCandidateOui(removedOui));
    std::cout << "flock_detection_test: PASS\n";
}

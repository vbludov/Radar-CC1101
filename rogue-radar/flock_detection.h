#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace rogue_radar {

// Passive-only 802.11 classifier. It never transmits or associates.
//
// Signature provenance, reviewed 2026-10-11:
// - colonelpanichacks/flock-you @ 6704f011f3e6c9cfe94fe18c593f863032c9c954
//   (address roles, wildcard probe + primary IE signature, firmware-derived
//   exact MACs and SSIDs).
// - nitekry/nite-oui-collection @ 29501d51cbfb1657396ccd7a2bf1445c93e2ceb1
//   (my_tested_flock.md confidence/status; removed prefixes stay excluded).
// - 0xXyc/flock-you-wifi-recon, now JakeSwiz/flock-you-wifi-recon,
//   @ 22d9672a1eaebaaf4e49e962d8cdc1f982d34f28
//   (addr2/source matching and the distinction between wildcard probe SSIDs
//   and hidden beacon/probe-response SSIDs).
//
// OUI matches are candidates, not proof that a device is Flock hardware.
// Receiver hits are second-hand evidence: the received frame's RSSI measures
// its transmitter, not the receiver address that matched.

enum class FlockWifiRole : uint8_t { Transmitter, Receiver };
enum class FlockWifiFrame : uint8_t {
    Management,
    Data,
    Beacon,
    ProbeRequest,
    ProbeResponse,
};
enum class FlockWifiEvidence : uint8_t {
    CuratedOui,
    WildcardProbeFromCuratedOui,
    PrimaryProbeFingerprint,
    ExactFirmwareMac,
    ExactFlockSsid,
    FlockSsidPattern,
    FlockNameHint,
};
enum class FlockWifiConfidence : uint8_t { Hint, Medium, High };

inline const char *flockWifiRoleLabel(FlockWifiRole value) {
    return value == FlockWifiRole::Transmitter ? "Transmitter" : "Receiver";
}
inline const char *flockWifiFrameLabel(FlockWifiFrame value) {
    switch (value) {
        case FlockWifiFrame::Management: return "Management";
        case FlockWifiFrame::Data: return "Data";
        case FlockWifiFrame::Beacon: return "Beacon";
        case FlockWifiFrame::ProbeRequest: return "Probe request";
        case FlockWifiFrame::ProbeResponse: return "Probe response";
    }
    return "Unknown";
}
inline const char *flockWifiEvidenceLabel(FlockWifiEvidence value) {
    switch (value) {
        case FlockWifiEvidence::CuratedOui: return "Curated OUI hint";
        case FlockWifiEvidence::WildcardProbeFromCuratedOui: return "Wildcard probe + OUI";
        case FlockWifiEvidence::PrimaryProbeFingerprint: return "Known probe fingerprint";
        case FlockWifiEvidence::ExactFirmwareMac: return "Firmware default MAC";
        case FlockWifiEvidence::ExactFlockSsid: return "Known SSID";
        case FlockWifiEvidence::FlockSsidPattern: return "Flock-XXXXXX SSID";
        case FlockWifiEvidence::FlockNameHint: return "SSID text hint";
    }
    return "Unknown";
}
inline const char *flockWifiConfidenceLabel(FlockWifiConfidence value) {
    switch (value) {
        case FlockWifiConfidence::Hint: return "Hint";
        case FlockWifiConfidence::Medium: return "Medium";
        case FlockWifiConfidence::High: return "High";
    }
    return "Unknown";
}

struct FlockWifiHit {
    uint8_t mac[6];
    char ssid[33];
    FlockWifiRole role;
    FlockWifiFrame frame;
    FlockWifiEvidence evidence;
    FlockWifiConfidence confidence;
    int8_t receivedRssi;
    uint8_t channel;
    bool rssiAppliesToDevice;
};

struct FlockWifiClassification {
    FlockWifiHit hits[2];
    uint8_t count;
    bool malformedInformationElements;
};

namespace flock_detection_detail {

struct Oui {
    uint8_t bytes[3];
};

// Active/high/solid and still-testing entries from the current curated file.
// Confidence differences inside that research set are intentionally flattened
// to a hint here; an OUI alone never produces High confidence.
static const Oui kCandidateOuis[] = {
    {{0x70,0xc9,0x4e}}, {{0x3c,0x91,0x80}}, {{0xd8,0xf3,0xbc}},
    {{0x80,0x30,0x49}}, {{0x14,0x5a,0xfc}}, {{0x74,0x4c,0xa1}},
    {{0x9c,0x2f,0x9d}}, {{0x94,0x08,0x53}}, {{0xe4,0xaa,0xea}},
    {{0xf4,0x6a,0xdd}}, {{0xe0,0x0a,0xf6}}, {{0x00,0xf4,0x8d}},
    {{0xd0,0x39,0x57}}, {{0xe8,0xd0,0xfc}}, {{0x58,0x8e,0x81}},
    {{0x08,0x3a,0x88}}, {{0x90,0x35,0xea}}, {{0x24,0xb2,0xb9}},
    {{0xc0,0x35,0x32}}, {{0xb8,0x35,0x32}}, {{0xe0,0x4f,0x43}},
    {{0xec,0x1b,0xbd}}, {{0x3c,0x71,0xbf}}, {{0xb8,0x1e,0xa4}},
    {{0x14,0xb5,0xcd}}, {{0x58,0x00,0xe3}}, {{0x70,0x08,0x94}},
    {{0x5c,0x93,0xa2}}, {{0x64,0x6e,0x69}}, {{0x48,0x27,0xea}},
    {{0xa4,0xcf,0x12}}, {{0x82,0x6b,0xf2}}, {{0xb4,0x1e,0x52}},
};

static bool equalMac(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

static bool candidateOui(const uint8_t *mac) {
    if (!mac || (mac[0] & 0x01U)) return false;
    for (size_t i = 0; i < sizeof(kCandidateOuis) / sizeof(kCandidateOuis[0]); ++i) {
        if (memcmp(mac, kCandidateOuis[i].bytes, 3) == 0) return true;
    }
    return false;
}

static bool exactFirmwareMac(const uint8_t *mac) {
    static const uint8_t exact[][6] = {
        {0x00,0x03,0x7f,0x50,0x00,0x01},
        {0x00,0x03,0x7f,0x4f,0x00,0x16},
    };
    return equalMac(mac, exact[0]) || equalMac(mac, exact[1]);
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
}

static bool equalsIgnoreCase(const char *value, size_t length, const char *expected) {
    const size_t expectedLength = strlen(expected);
    if (length != expectedLength) return false;
    for (size_t i = 0; i < length; ++i)
        if (lower(value[i]) != lower(expected[i])) return false;
    return true;
}

static bool containsIgnoreCase(const char *value, size_t length, const char *needle) {
    const size_t needleLength = strlen(needle);
    if (!needleLength || needleLength > length) return false;
    for (size_t i = 0; i + needleLength <= length; ++i) {
        size_t j = 0;
        while (j < needleLength && lower(value[i + j]) == lower(needle[j])) ++j;
        if (j == needleLength) return true;
    }
    return false;
}

static bool strictFlockSsid(const char *ssid, size_t length) {
    if (length != 12 || !equalsIgnoreCase(ssid, 6, "Flock-")) return false;
    for (size_t i = 6; i < 12; ++i) {
        const char c = ssid[i];
        if (!((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'f')))
            return false;
    }
    return true;
}

struct InformationElements {
    char ssid[33];
    uint8_t ssidLength;
    bool hasSsid;
    bool wildcardSsid;
    bool malformed;
    bool primaryFingerprint;
};

static InformationElements parseInformationElements(const uint8_t *data, size_t length) {
    InformationElements result{};
    uint8_t signatureStep = 0;
    while (length >= 2) {
        const uint8_t id = data[0];
        const uint8_t elementLength = data[1];
        if (size_t(elementLength) + 2U > length) {
            result.malformed = true;
            break;
        }
        const uint8_t *body = data + 2;
        if (id == 0 && !result.hasSsid) {
            result.hasSsid = true;
            result.wildcardSsid = elementLength == 0;
            if (elementLength > 32) result.malformed = true;
            result.ssidLength = elementLength > 32 ? 32 : elementLength;
            if (result.ssidLength) memcpy(result.ssid, body, result.ssidLength);
            result.ssid[result.ssidLength] = '\0';
        } else if (result.wildcardSsid && signatureStep < 7) {
            static const uint8_t expectedIds[] = {2, 12, 127, 221, 45, 191, 221};
            bool matches = id == expectedIds[signatureStep];
            if (matches && signatureStep == 3) {
                static const uint8_t vendor[] = {0x50,0x6f,0x9a,0x16,0x03,0x01,0x03};
                matches = elementLength >= sizeof(vendor) &&
                          memcmp(body, vendor, sizeof(vendor)) == 0;
            } else if (matches && signatureStep == 6) {
                static const uint8_t vendor[] = {0x00,0x50,0xf2,0x08,0x00,0x00,0x00};
                matches = elementLength >= sizeof(vendor) &&
                          memcmp(body, vendor, sizeof(vendor)) == 0;
            }
            signatureStep = matches ? uint8_t(signatureStep + 1U) : 0;
        }
        data += size_t(elementLength) + 2U;
        length -= size_t(elementLength) + 2U;
    }
    if (length != 0) result.malformed = true;
    result.primaryFingerprint = !result.malformed && result.wildcardSsid &&
                                signatureStep == 7;
    return result;
}

static FlockWifiFrame frameKind(uint8_t type, uint8_t subtype) {
    if (type == 2) return FlockWifiFrame::Data;
    if (subtype == 8) return FlockWifiFrame::Beacon;
    if (subtype == 4) return FlockWifiFrame::ProbeRequest;
    if (subtype == 5) return FlockWifiFrame::ProbeResponse;
    return FlockWifiFrame::Management;
}

static void addHit(FlockWifiClassification &out, const uint8_t *mac,
                   FlockWifiRole role, FlockWifiFrame frame,
                   FlockWifiEvidence evidence, FlockWifiConfidence confidence,
                   int8_t rssi, uint8_t channel, const char *ssid) {
    if (out.count >= 2) return;
    FlockWifiHit &hit = out.hits[out.count++];
    memset(&hit, 0, sizeof(hit));
    memcpy(hit.mac, mac, 6);
    if (ssid) {
        strncpy(hit.ssid, ssid, sizeof(hit.ssid) - 1);
        hit.ssid[sizeof(hit.ssid) - 1] = '\0';
    }
    hit.role = role;
    hit.frame = frame;
    hit.evidence = evidence;
    hit.confidence = confidence;
    hit.receivedRssi = rssi;
    hit.channel = channel;
    hit.rssiAppliesToDevice = role == FlockWifiRole::Transmitter;
}

}  // namespace flock_detection_detail

// Shared conservative prefix check for BLE and WiFi callers. A true result is
// only a candidate OUI hint; it must not be presented as confirmed Flock.
inline bool flockCandidateOui(const uint8_t mac[6]) {
    return flock_detection_detail::candidateOui(mac);
}

inline bool classifyFlockWifiFrame(const uint8_t *frame, size_t length,
                                   int8_t receivedRssi, uint8_t channel,
                                   FlockWifiClassification &out) {
    using namespace flock_detection_detail;
    memset(&out, 0, sizeof(out));
    if (!frame || length < 24) return false;

    if ((frame[0] & 0x03U) != 0) return false;  // unsupported 802.11 version

    const uint8_t type = uint8_t((frame[0] >> 2) & 0x03U);
    const uint8_t subtype = uint8_t((frame[0] >> 4) & 0x0fU);
    if (type != 0 && type != 2) return false;
    if (type == 2) {
        const bool fourAddress = (frame[1] & 0x03U) == 0x03U;
        const bool qos = (subtype & 0x08U) != 0;
        const size_t minimum = 24U + (fourAddress ? 6U : 0U) + (qos ? 2U : 0U);
        if (length < minimum) return false;
    }

    const uint8_t *receiver = frame + 4;
    const uint8_t *transmitter = frame + 10;
    const FlockWifiFrame kind = frameKind(type, subtype);
    InformationElements ies{};
    bool parsedIes = false;
    if (type == 0) {
        size_t offset = 0;
        if (subtype == 4) offset = 24;
        else if (subtype == 8 || subtype == 5) offset = 36;
        if (offset) {
            if (length < offset) return false;
            ies = parseInformationElements(frame + offset, length - offset);
            out.malformedInformationElements = ies.malformed;
            parsedIes = true;
        }
    }

    FlockWifiEvidence txEvidence = FlockWifiEvidence::CuratedOui;
    FlockWifiConfidence txConfidence = FlockWifiConfidence::Hint;
    bool txHit = false;
    if (exactFirmwareMac(transmitter)) {
        txEvidence = FlockWifiEvidence::ExactFirmwareMac;
        txConfidence = FlockWifiConfidence::High;
        txHit = true;
    }
    // A directed probe request names the AP a client is seeking. Its SSID is
    // not the transmitter's identity, so SSID-only evidence is intentionally
    // limited to beacon/probe-response transmitters.
    if (parsedIes && !ies.malformed && kind != FlockWifiFrame::ProbeRequest &&
        ies.hasSsid && ies.ssidLength) {
        if (strictFlockSsid(ies.ssid, ies.ssidLength)) {
            if (txConfidence != FlockWifiConfidence::High) {
                txEvidence = FlockWifiEvidence::FlockSsidPattern;
                txConfidence = FlockWifiConfidence::High;
            }
            txHit = true;
        } else if (equalsIgnoreCase(ies.ssid, ies.ssidLength, "Flock") ||
                   equalsIgnoreCase(ies.ssid, ies.ssidLength, "FS Ext Battery") ||
                   equalsIgnoreCase(ies.ssid, ies.ssidLength, "test_flck")) {
            if (txConfidence != FlockWifiConfidence::High) {
                txEvidence = FlockWifiEvidence::ExactFlockSsid;
                txConfidence = FlockWifiConfidence::Medium;
            }
            txHit = true;
        } else if (containsIgnoreCase(ies.ssid, ies.ssidLength, "flock") ||
                   containsIgnoreCase(ies.ssid, ies.ssidLength, "flck") ||
                   containsIgnoreCase(ies.ssid, ies.ssidLength, "penguin") ||
                   containsIgnoreCase(ies.ssid, ies.ssidLength, "pigvision")) {
            if (!txHit) {
                txEvidence = FlockWifiEvidence::FlockNameHint;
                txConfidence = FlockWifiConfidence::Hint;
            }
            txHit = true;
        }
    }
    if (candidateOui(transmitter)) {
        if (kind == FlockWifiFrame::ProbeRequest && ies.wildcardSsid &&
            ies.primaryFingerprint) {
            txEvidence = FlockWifiEvidence::PrimaryProbeFingerprint;
            txConfidence = FlockWifiConfidence::High;
        } else if (kind == FlockWifiFrame::ProbeRequest && !ies.malformed &&
                   ies.wildcardSsid &&
                   txConfidence != FlockWifiConfidence::High) {
            txEvidence = FlockWifiEvidence::WildcardProbeFromCuratedOui;
            txConfidence = FlockWifiConfidence::Medium;
        } else if (!txHit) {
            txEvidence = FlockWifiEvidence::CuratedOui;
            txConfidence = FlockWifiConfidence::Hint;
        }
        txHit = true;
    }
    if (txHit) addHit(out, transmitter, FlockWifiRole::Transmitter, kind,
                      txEvidence, txConfidence, receivedRssi, channel,
                      ies.hasSsid ? ies.ssid : nullptr);

    if (candidateOui(receiver) || exactFirmwareMac(receiver)) {
        addHit(out, receiver, FlockWifiRole::Receiver, kind,
               exactFirmwareMac(receiver) ? FlockWifiEvidence::ExactFirmwareMac
                                          : FlockWifiEvidence::CuratedOui,
               exactFirmwareMac(receiver) ? FlockWifiConfidence::Medium
                                          : FlockWifiConfidence::Hint,
               receivedRssi, channel, nullptr);
    }
    return out.count != 0;
}

}  // namespace rogue_radar

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "flock_detection.h"

namespace rogue_radar {

enum class FlockBleEvidence : uint8_t {
    CuratedOui,
    FlockNameHint,
    PenguinSerialName,
    BareSerialName,
    ExternalBatteryName,
    DfuName,
    XuntongCompanyId,
    XuntongSerialPayload,
    FlockAccessoryService,
    NordicDfuService,
    RavenServiceRange,
};

enum class FlockBleDeviceType : uint8_t {
    Candidate,
    BatteryPack,
    FlockAccessory,
    Raven,
};

struct FlockBleObservation {
    const char *name;
    const uint8_t *mac;
    uint8_t addressType;
    const uint8_t *manufacturerData;
    size_t manufacturerDataLength;
    const char *const *serviceUuids;
    size_t serviceUuidCount;
};

struct FlockBleClassification {
    bool matched;
    FlockBleEvidence evidence;
    FlockBleDeviceType deviceType;
    FlockWifiConfidence confidence;
    uint8_t mac[6];
    uint8_t addressType;
    char name[33];
};

inline const char *flockBleEvidenceLabel(FlockBleEvidence value) {
    switch (value) {
        case FlockBleEvidence::CuratedOui: return "Curated OUI hint";
        case FlockBleEvidence::FlockNameHint: return "Flock name hint";
        case FlockBleEvidence::PenguinSerialName: return "Penguin serial name";
        case FlockBleEvidence::BareSerialName: return "10-digit name hint";
        case FlockBleEvidence::ExternalBatteryName: return "FS Ext Battery name";
        case FlockBleEvidence::DfuName: return "DFU name hint";
        case FlockBleEvidence::XuntongCompanyId: return "XUNTONG company ID hint";
        case FlockBleEvidence::XuntongSerialPayload: return "XUNTONG TN serial";
        case FlockBleEvidence::FlockAccessoryService: return "Flock accessory service";
        case FlockBleEvidence::NordicDfuService: return "Nordic DFU service hint";
        case FlockBleEvidence::RavenServiceRange: return "Raven service-range hint";
    }
    return "Unknown";
}

inline const char *flockBleDeviceTypeLabel(FlockBleDeviceType value) {
    switch (value) {
        case FlockBleDeviceType::Candidate: return "Flock candidate";
        case FlockBleDeviceType::BatteryPack: return "Flock battery candidate";
        case FlockBleDeviceType::FlockAccessory: return "Flock accessory";
        case FlockBleDeviceType::Raven: return "Raven candidate";
    }
    return "Flock candidate";
}

namespace flock_ble_detection_detail {

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
}

static bool equalIgnoreCase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (lower(*a++) != lower(*b++)) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool containsIgnoreCase(const char *value, const char *needle) {
    if (!value || !needle || !*needle) return false;
    const size_t valueLength = strlen(value);
    const size_t needleLength = strlen(needle);
    if (needleLength > valueLength) return false;
    for (size_t i = 0; i + needleLength <= valueLength; ++i) {
        size_t j = 0;
        while (j < needleLength && lower(value[i + j]) == lower(needle[j])) ++j;
        if (j == needleLength) return true;
    }
    return false;
}

static bool digits(const char *value, size_t count) {
    if (!value) return false;
    for (size_t i = 0; i < count; ++i)
        if (value[i] < '0' || value[i] > '9') return false;
    return true;
}

static bool penguinName(const char *name) {
    if (!name || strlen(name) != 18) return false;
    static const char prefix[] = "penguin-";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
        if (lower(name[i]) != prefix[i]) return false;
    return digits(name + 8, 10);
}

static bool bareSerialName(const char *name) {
    return name && strlen(name) == 10 && digits(name, 10);
}

static bool xuntong(const uint8_t *data, size_t length) {
    return data && length >= 2 && data[0] == 0xc8 && data[1] == 0x09;
}

static bool tnSerial(const uint8_t *data, size_t length) {
    if (!xuntong(data, length) || length < 14) return false;
    for (size_t i = 2; i + 12 <= length; ++i) {
        if (data[i] != 'T' || data[i + 1] != 'N') continue;
        size_t digitCount = 0;
        for (size_t j = i + 2; j < length && data[j] >= '0' && data[j] <= '9'; ++j)
            ++digitCount;
        if (digitCount >= 10) return true;
    }
    return false;
}

static int hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    value = lower(value);
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

static bool ravenService(const char *uuid) {
    if (!uuid) return false;
    const char *digitsValue = uuid;
    size_t count = strlen(uuid);
    if (count == 6 && uuid[0] == '0' && lower(uuid[1]) == 'x') {
        digitsValue += 2;
        count = 4;
    } else if (count == 36 && uuid[8] == '-' && uuid[0] == '0' &&
               uuid[1] == '0' && uuid[2] == '0' && uuid[3] == '0') {
        digitsValue += 4;
        count = 4;
    }
    if (count != 4) return false;
    uint16_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const int nibble = hex(digitsValue[i]);
        if (nibble < 0) return false;
        value = uint16_t((value << 4) | uint16_t(nibble));
    }
    return value >= 0x3100 && value <= 0x3500;
}

static uint8_t evidenceSpecificity(FlockBleEvidence evidence) {
    switch (evidence) {
        case FlockBleEvidence::CuratedOui: return 0;
        case FlockBleEvidence::FlockNameHint:
        case FlockBleEvidence::BareSerialName:
        case FlockBleEvidence::DfuName:
        case FlockBleEvidence::XuntongCompanyId:
        case FlockBleEvidence::NordicDfuService:
        case FlockBleEvidence::RavenServiceRange: return 1;
        case FlockBleEvidence::PenguinSerialName:
        case FlockBleEvidence::ExternalBatteryName:
        case FlockBleEvidence::XuntongSerialPayload: return 2;
        case FlockBleEvidence::FlockAccessoryService: return 3;
    }
    return 0;
}

static void set(FlockBleClassification &out, FlockBleEvidence evidence,
                FlockBleDeviceType deviceType, FlockWifiConfidence confidence) {
    if (out.matched && (uint8_t(out.confidence) > uint8_t(confidence) ||
        (out.confidence == confidence &&
         evidenceSpecificity(out.evidence) >= evidenceSpecificity(evidence)))) return;
    out.matched = true;
    out.evidence = evidence;
    out.deviceType = deviceType;
    out.confidence = confidence;
}

}  // namespace flock_ble_detection_detail

inline bool classifyFlockBle(const FlockBleObservation &observation,
                             FlockBleClassification &out) {
    using namespace flock_ble_detection_detail;
    memset(&out, 0, sizeof(out));
    out.addressType = observation.addressType;
    if (observation.mac) memcpy(out.mac, observation.mac, sizeof(out.mac));
    if (observation.name) {
        strncpy(out.name, observation.name, sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
    }

    // A random/private address can coincidentally begin with a curated prefix.
    // Apply OUI evidence only to public and public-identity address types.
    if (observation.mac && (observation.addressType == 0 || observation.addressType == 2) &&
        flockCandidateOui(observation.mac))
        set(out, FlockBleEvidence::CuratedOui, FlockBleDeviceType::Candidate,
            FlockWifiConfidence::Hint);

    if (penguinName(observation.name))
        set(out, FlockBleEvidence::PenguinSerialName, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Medium);
    else if (equalIgnoreCase(observation.name, "FS Ext Battery"))
        set(out, FlockBleEvidence::ExternalBatteryName, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Medium);
    else if (bareSerialName(observation.name))
        set(out, FlockBleEvidence::BareSerialName, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Hint);
    else if (equalIgnoreCase(observation.name, "DfuTarg"))
        set(out, FlockBleEvidence::DfuName, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Hint);
    else if (containsIgnoreCase(observation.name, "flock"))
        set(out, FlockBleEvidence::FlockNameHint, FlockBleDeviceType::Candidate,
            FlockWifiConfidence::Hint);

    if (tnSerial(observation.manufacturerData, observation.manufacturerDataLength))
        set(out, FlockBleEvidence::XuntongSerialPayload, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Medium);
    else if (xuntong(observation.manufacturerData, observation.manufacturerDataLength))
        set(out, FlockBleEvidence::XuntongCompanyId, FlockBleDeviceType::BatteryPack,
            FlockWifiConfidence::Hint);

    const size_t serviceCount = observation.serviceUuidCount < 16
        ? observation.serviceUuidCount : 16;
    for (size_t i = 0; observation.serviceUuids && i < serviceCount; ++i) {
        const char *uuid = observation.serviceUuids[i];
        if (equalIgnoreCase(uuid, "e8ccbb38-9532-46a8-9fe5-1814df172e6f")) {
            set(out, FlockBleEvidence::FlockAccessoryService,
                FlockBleDeviceType::FlockAccessory, FlockWifiConfidence::High);
        } else if (equalIgnoreCase(uuid, "00001530-1212-efde-1523-785feabcd123")) {
            set(out, FlockBleEvidence::NordicDfuService,
                FlockBleDeviceType::BatteryPack, FlockWifiConfidence::Hint);
        } else if (ravenService(uuid)) {
            set(out, FlockBleEvidence::RavenServiceRange,
                FlockBleDeviceType::Raven, FlockWifiConfidence::Hint);
        }
    }
    return out.matched;
}

}  // namespace rogue_radar

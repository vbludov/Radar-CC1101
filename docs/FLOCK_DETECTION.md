# Passive Flock detection provenance

This detector classifies radio observations as evidence with explicit confidence. It does not actively probe, associate, or claim that a general-purpose vendor prefix proves a device is Flock hardware.

The signature review was refreshed on 2026-10-11 against the then-current default branches:

- [colonelpanichacks/flock-you at `6704f01` (2026-09-17)](https://github.com/colonelpanichacks/flock-you/tree/6704f011f3e6c9cfe94fe18c593f863032c9c954): transmitter/receiver address roles, wildcard probe and primary IE fingerprint, and firmware-derived Wi-Fi and BLE signatures. The detailed firmware provenance is in its [`firmware_derived_signatures.md`](https://github.com/colonelpanichacks/flock-you/blob/6704f011f3e6c9cfe94fe18c593f863032c9c954/datasets/firmware_derived_signatures.md).
- [nitekry/nite-oui-collection at `29501d5` (2026-07-16)](https://github.com/nitekry/nite-oui-collection/blob/29501d51cbfb1657396ccd7a2bf1445c93e2ceb1/groups/flockers/my_tested_flock.md): active, tentative, disputed, and removed community prefix evidence.
- [0xXyc/flock-you-wifi-recon, now JakeSwiz/flock-you-wifi-recon, at `22d9672` (2026-07-27)](https://github.com/JakeSwiz/flock-you-wifi-recon/tree/22d9672a1eaebaaf4e49e962d8cdc1f982d34f28): field-observed source-address probe requests and community prefixes retained below positive-identification confidence.

## Wi-Fi interpretation

In the 802.11 header, address 2 is the transmitter and address 1 is the receiver. A match on address 1 is second-hand evidence: the captured RSSI belongs to the transmitter, such as an access point replying to a station, and must not be displayed as the receiver candidate's signal strength.

An empty SSID element in a probe request is a wildcard query. It is normal client behavior and differs from a hidden SSID in a beacon or probe response. A directed probe request containing a Flock-like network name identifies what the client is seeking, not the client itself, so SSID-only probe-request matches are suppressed.

High confidence is reserved for stronger identity evidence implemented in `flock_detection.h`: the exact primary wildcard-probe IE fingerprint with a curated transmitter prefix, the exact firmware-default MACs, or a transmitted `Flock-` name followed by exactly six hexadecimal characters. A curated OUI alone remains a hint. Removed `cc:cc:cc`, `f8:a2:d6`, and disputed `00:0c:e7` entries are excluded. Malformed or oversized information elements cannot produce name or fingerprint confidence.

## BLE interpretation

`flock_ble_detection.h` gives High confidence only to the exact Flock accessory service UUID. Exact `Penguin-` plus ten digits, `FS Ext Battery`, and XUNTONG manufacturer data containing a `TN` serial shape are Medium evidence. A bare ten-digit name, `DfuTarg`, the generic Nordic DFU UUID, XUNTONG company ID without its serial shape, the broad Raven service range, and a curated MAC prefix remain hints because they can occur on unrelated devices.

Explicit BLE names containing `Flock`, and legacy Wi-Fi names containing `Penguin` or `pigvision`, remain visible as hints only. A directed probe request carrying one of those Wi-Fi names is still ignored because it identifies the network a client seeks rather than the client itself. BLE OUI evidence is used only for public or public-identity addresses; random/private and unknown address types cannot be treated as manufacturer-prefix evidence.

Generic `Android` and `msm8953_32` names never match on their own. BLE address type remains part of the captured identity because the same address bytes with a different address type are not necessarily the same advertiser.

The classifiers return bounded value types and do no allocation. Callers may queue the returned evidence, apply alert cooldowns, and render the supplied role, evidence, confidence, and device-type labels outside radio callbacks.

## Firmware behavior and limits

Medium/High evidence can trigger the enabled sound/light alerts through the existing repeat-alert filter. Hint-only matches stay visible without alerts. The Wi-Fi and BLE callback queues are fixed at 16 entries each, coalesce repeated observations, and drain in bounded batches outside callbacks. Overflow retires the oldest queued identity. Results remain bounded at 20 entries in Flock Detector and 30 in Flock Hybrid.

Flock Hybrid alternates Wi-Fi and BLE listening; it cannot observe both at once. Channel hopping also leaves gaps on individual Wi-Fi channels. Silent devices, devices outside reception range, and activity confined to cellular or 5 GHz radios cannot be detected by these ESP32-S3 tools. Signature tests exercise parsing and classification; they do not establish a measured field detection rate or guarantee that every camera is detectable.

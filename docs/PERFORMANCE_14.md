# Crowded-list and long-running performance investigation

Version: `v1.1.0-cc1101.14` · Investigation date: 2026-10-11

## Findings and changes

The reported slowdown after several days has not yet been reproduced under a multi-day controlled run. A read-only USB capture from the previous firmware produced no runtime diagnostics. The following repeated work was established by source inspection and is addressed in this release:

| Area | Previous behavior | Change |
|---|---|---|
| Display allocator | A full Hybrid list left 11,840 bytes free, but its largest free block was only 7,720 bytes. Rendering needed 8,960 contiguous bytes and repeatedly retried. | Add a 128 KB PSRAM drawing pool on CC1101, alongside the existing 64 KB pool, leaving internal radio memory available. Configure the pinned LVGL allocator to accept that pool. |
| BLE result lists | A new identity at the fixed result limit was treated as list growth, causing a complete rebuild under address churn. | Update existing rows at capacity; protect the focused identity; process at most eight queued observations per loop; remove redundant callback string copies/classification. |
| Nearby Signals | Frequent unchanged chart/label writes and repeated synchronous SD library searches for saved markers. | Paint at most once per second unless explicitly invalidated; skip unchanged values; cache saved/unsaved results until library changes; resolve queued lookups outside paint, one directory entry/logical record per loop. |
| Saved marker search | Repeated sorted paging and repeated record reads on paint paths. | One linear directory pass for a batch of identities, with corruption/recovery handling and no directory mutation during enumeration. Missing-card failures back off for 30 seconds. |
| Flock Detector / Hybrid | Single pending observations dropped bursts; full-list/group rebuilds and sorting during refresh. | Fixed coalescing queues, bounded draining, persistent rows, stable Hybrid selection, and bounded BTC-owned decoding workspace outside the Bluetooth task stack. |
| WiFi Mapper | Recreated its grid and up to 80 points every half second. | Reuse the grid and bounded point pool; correct plot dimensions to fit the container. |
| Network Scanner / PineAP | Rebuilt groups and rows after every scan. PineAP also counted unretained SSIDs repeatedly and touched the alert cache before a detection. | Reuse rows, preserve focused BSSID/control, bound retained SSID evidence, and alert on the detection threshold crossing. |
| Pwnagotchi / Deauth | Recreated rows during periodic refresh. | Keep rows and focus; skip unchanged event lists. Pwnagotchi identity uses BSSID rather than advertised name. |

SD records and live results have bounded in-memory representations. The hardware capacity test reproduced a drawing-allocation retry stall; it did not establish an unbounded result-list memory leak. Repeated allocation and SD work explain avoidable busy-list load, but do not by themselves prove the precise cause of the user's multi-day symptom. One SD record may still require multiple physical reads; the incremental lookup is not a hard real-time guarantee.

## Runtime observations

Normal firmware emits a `[Health]` line at most once a minute when a USB serial host is connected and has transmit-buffer space. It reports uptime, scan key, average/maximum loop processing time in microseconds (excluding the final 5 ms delay), free/minimum internal heap, largest internal allocation block, and LVGL free/largest blocks. It does not write logs to SD or include detected device identities. Comparing these values over hours helps distinguish persistent memory decline from intermittent blocking work. A skipped log does not stop scanning.

## Validation

- Host classifier tests include truncated/malformed Wi-Fi frames, address roles, directed versus wildcard probes, confidence, BLE manufacturer/service patterns, and private-address handling.
- Queue tests cover 200,000 observations, coalescing, saturation, overflow order, and identity separation.
- Storage/cache tests cover linear traversal, recovery candidates, and zero filesystem work on repeated resolved paint lookups.
- Pinned LVGL tests cover nested saved/nearby UI navigation and asynchronous lookup integration.
- The optional `ROGUE_RADAR_PERFORMANCE_TEST=1` device diagnostic uses production BLE/Flock/Hybrid update functions at capacity, injects 600 replacement identities per tool, and checks row/group reuse, focus, and memory.
- On CC1101, the same 1,800-observation capacity test passed after adding the pool. Internal heap remained 123,884 bytes throughout each synthetic churn stage. Hybrid ended with 143,048 bytes of LVGL free space and a 131,064-byte largest free block; no drawing-allocation retry warnings occurred in that repeat. These are synthetic capacity results, not a multi-day soak.
- Both `original` and `t_embed_cc1101` firmware profiles build. The final normal application excludes automatic device-test code.
- The device passed repeated lifecycle checks on all 20 scanning pages and the 90-second BLE run. An initial Hybrid run exposed a BTC task stack overflow in the new signature adapter. Moving its bounded scratch arrays outside the serialized callback stack reduced the compiled callback frame to 192 bytes (classifier: 48 bytes). The focused 90-second Hybrid retest passed through repeated BLE/Wi-Fi phases and active Back. It processed 3,182 BLE advertisements, retained at least 804 bytes of BTC stack headroom, and held internal heap at 27,560 bytes after radio initialization. No allocation warnings or crashes occurred in that retest; physical Flock identity matches were not present in this local test.
- A fresh pinned-LVGL host build reproduces an 8,960-byte allocation failure with the original pool and retained-menu/full-Hybrid workload; the expanded pool passes focused rendering without allocation warnings. The retained-Pwn-row focus regression also passes.

- The normal `.14` application was flashed over USB with write verification and booted successfully, including the supplemental drawing pool. Application SHA-256: `70cbb87839420afd4c861692f5be08ea5c6dd7895b5e14c47e11241531ba02e9`.

See [Flock evidence and sources](FLOCK_DETECTION.md) for the detection update. Physical Flock-camera coverage and multi-day endurance remain field-validation items.

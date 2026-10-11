#include "../rogue-radar/flock_observation_queue.h"
#include <assert.h>
#include <stdio.h>
using namespace rogue_radar;
int main() {
    FlockObservationQueue<16> q;
    FlockObservation in, out;
    snprintf(in.mac, sizeof(in.mac), "02:00:00:00:00:01");
    snprintf(in.confidence, sizeof(in.confidence), "High");
    in.rssi = -70; q.push(in);
    snprintf(in.confidence, sizeof(in.confidence), "Hint");
    in.rssi = -30;
    for (unsigned i = 0; i < 100000; ++i) q.push(in);
    assert(q.size() == 1 && q.drops() == 0);
    assert(q.pop(out) && out.count == UINT16_MAX);
    assert(!strcmp(out.confidence, "High") && out.rssi == -30);
    assert(FlockObservationQueue<16>::rank("Hint") < FlockObservationQueue<16>::rank("Medium"));
    q.push(in); in.receiverOnly = true; q.push(in);
    assert(q.size() == 2); // receiver and transmitter evidence cannot overwrite each other
    in.addressType = 1; q.push(in);
    assert(q.size() == 3); // identical bytes with a different BLE address type stay distinct
    q.reset();
    for (unsigned i = 0; i < 100000; ++i) {
        snprintf(in.mac, sizeof(in.mac), "02:00:%02X:%02X:%02X:%02X", (i >> 24) & 255,
                 (i >> 16) & 255, (i >> 8) & 255, i & 255);
        in.seenMs = i; q.push(in);
        assert(q.size() <= 16);
    }
    assert(q.drops() == 100000 - 16);
    for (unsigned i = 100000 - 16; i < 100000; ++i) {
        assert(q.pop(out) && out.seenMs == i);
    }
    assert(!q.pop(out));
    q.reset(); assert(q.drops() == 0 && q.size() == 0);
    puts("PASS Flock bounded queue: 200000 observations, coalescing, evidence, overflow, saturation");
}

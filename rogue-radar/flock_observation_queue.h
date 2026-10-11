#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace rogue_radar {
struct FlockObservation {
    char name[33]{}, mac[18]{}, method[24]{}, confidence[8]{}, type[24]{};
    int8_t rssi = -127;
    uint8_t frame = 0;
    uint8_t addressType = 255;
    bool receiverOnly = false;
    uint16_t count = 1;
    uint32_t seenMs = 0;
};

// Caller owns synchronization. No allocation, and repeated frames from one
// identity cannot occupy every slot. Overflow evicts the oldest queued identity.
template <size_t Capacity> class FlockObservationQueue {
    static_assert(Capacity > 0, "queue needs storage");
    FlockObservation entries_[Capacity]{};
    size_t head_ = 0, count_ = 0;
    uint32_t drops_ = 0;
public:
    void reset() { head_ = count_ = 0; drops_ = 0; }
    size_t size() const { return count_; }
    uint32_t drops() const { return drops_; }
    void push(const FlockObservation &item) {
        for (size_t i = 0; i < count_; ++i) {
            auto &old = entries_[(head_ + i) % Capacity];
            if (old.receiverOnly != item.receiverOnly || old.addressType != item.addressType ||
                strcmp(old.mac, item.mac)) continue;
            const uint32_t total = uint32_t(old.count) + item.count;
            // Preserve stronger evidence through weaker repeats in the queue.
            const bool preserve = rank(old.confidence) > rank(item.confidence);
            if (!preserve) {
                old = item;
            } else {
                old.rssi = item.rssi;
                old.seenMs = item.seenMs;
            }
            old.count = total > UINT16_MAX ? UINT16_MAX : uint16_t(total);
            return;
        }
        if (count_ == Capacity) {
            head_ = (head_ + 1) % Capacity;
            --count_; ++drops_;
        }
        entries_[(head_ + count_) % Capacity] = item;
        ++count_;
    }
    bool pop(FlockObservation &item) {
        if (!count_) return false;
        item = entries_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }
    static unsigned rank(const char *confidence) {
        return !strcmp(confidence, "High") ? 3 : !strcmp(confidence, "Medium") ? 2 : 1;
    }
};
} // namespace rogue_radar

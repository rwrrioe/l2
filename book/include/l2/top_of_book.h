#pragma once
#include <l2/common/types.h>

struct alignas(64) TopOfBook {
    Ticks bid_px = kInvalidTick;
    Qty bid_qty = Qty{0};
    Ticks ask_px = kInvalidTick;
    Qty ask_qty = Qty{0};
    uint64_t seq = 0;
    uint64_t _pad[3] = {};
};
static_assert(sizeof(TopOfBook) == 64);
static_assert(alignof(TopOfBook) == 64);


constexpr bool has_both_sides(const TopOfBook& t) noexcept {
    return t.bid_px != kInvalidTick && t.ask_px != kInvalidTick;
}

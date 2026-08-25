#pragma once
#include <cstdint>
#include <algorithm>
#include <l2/common/types.h>
#include <span>

struct DepthEvent {
    uint64_t u;
    uint64_t U;
    uint64_t pu;
    static constexpr uint64_t kNoPu = ~0ull;

    uint64_t event_time_us;
    uint64_t rx_timestamp_ns;

    SymbolId symbol;

    std::span<const LevelUpdate> bids;
    std::span<const LevelUpdate> asks;
};

struct SnapshotEvent {
    uint64_t last_update_id;
    uint64_t rx_timestamp_ns;
    SymbolId symbol;

    std::span<const LevelUpdate>bids;
    std::span<const LevelUpdate> asks;
};

enum class ParseError : uint8_t {
    BadJSON, UnknownStream, TooManyLevels, BadDecimal, PriceOffTick
};

class EventBuffer {
    struct StoredHeader {
        uint64_t U,u,pu;
        uint64_t event_time_us;
        uint64_t rx_timestamp_ns;

        SymbolId symbol;
        uint32_t bids_off, bids_len;
        uint32_t asks_off, asks_len;
    };

    static constexpr size_t kMaxEvents = 4096;
    static constexpr size_t kMaxLevelsTotal = 256 * 1024;

    std::array<StoredHeader, kMaxEvents> headers_;
    std::array<LevelUpdate, kMaxLevelsTotal> levels_;

    size_t n_events_ = 0;
    size_t n_levels_ = 0;
public:
        [[nodiscard]] bool push (const DepthEvent& e) noexcept {
            const size_t need = e.bids.size() + e.asks.size();

            if (n_events_ == kMaxEvents || n_levels_ + need > kMaxLevelsTotal) {
                return false;
            }

            auto& h = headers_[n_events_];
            h = {
                e.U,
                e.u,
                e.pu,
                e.event_time_us,
                e.rx_timestamp_ns,
                e.symbol,
                uint32_t(n_levels_),
                uint32_t(e.bids.size()),
                uint32_t(n_levels_ + e.bids.size()),
                uint32_t(e.asks.size()),
            };

            std::copy(e.bids.begin(), e.bids.end(), levels_.data() + n_levels_);
            n_levels_ += e.bids.size();

            std::copy(e.asks.begin(), e.asks.end(), levels_.data() + n_levels_);
            n_levels_ += e.asks.size();

            ++n_events_;
            return true;
        }

        DepthEvent view (size_t i) const noexcept {
            const auto& h = headers_[i];

            return {
                h.U,
                h.u,
                h.pu,
                h.event_time_us,
                h.rx_timestamp_ns,
                h.symbol,
                {levels_.data() + h.bids_off, h.bids_len},
                {levels_.data() + h.asks_off, h.asks_len},
            };
        }

        size_t size() const noexcept { return n_events_;}
        void clear() noexcept {n_events_ = 0; n_levels_ = 0;}
};

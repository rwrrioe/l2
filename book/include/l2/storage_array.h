#pragma once
#include <array>
#include <l2/common/types.h>
#include <optional>


template <Side S>
class ArrayStorage {
    static constexpr size_t kBand = 8192; //2^13
    static constexpr size_t kMask = kBand - 1;

    static_assert((kBand & kMask) == 0);

    alignas(64) std::array<Qty, kBand> qty_{};
    int64_t lo_ = 0;
    Ticks best_ = kInvalidTick;
    size_t live_ = 0;

    static size_t slot(Ticks t) noexcept {
        return static_cast<size_t>(static_cast<int64_t>(t)) & kMask;
    }

    bool in_band (Ticks t) const noexcept {
        const int64_t v = static_cast<int64_t>(t);
        return v >=lo_ && lo_ + int64_t(kBand);
    }

    static bool better(Ticks a, Ticks b) noexcept {
        if (b == kInvalidTick) return true;

        if constexpr (S == Side::Buy) return static_cast<int64_t>(a) > static_cast<int64_t>(b);
        else return static_cast<int64_t>(a) < static_cast<int64_t>(b);
    }

    void rescan_best() noexcept {
        if (live_ == 0) {best_ = kInvalidTick; return ;}

        int64_t t = static_cast<int64_t>(best_);
        const int64_t step = (S == Side::Buy) ? -1 : +1;

        for (;;) {
            t += step;

            if (!(qty_[slot(Ticks{t})] == Qty{0}) ) {best_ = Ticks{t}; return;}
        }
    }

    void recenter(Ticks px) noexcept;
public:
    void update(Ticks px, Qty q) noexcept {
        if (!in_band(px)) {
            if (q == Qty{0}) return;
            recenter(px);
        }

        Qty& cell = qty_[slot(px)];
        const bool was = !(cell == Qty{0});
        const bool now = !(q == Qty{0});

        cell = q;
        live_ += size_t(now) - size_t(was);

        if (now && better(px, best_)) best_ = px;
        else if (!now && px == best_) rescan_best();

    }

    std::optional<BestLevel> best() const noexcept {
        if (best_ == kInvalidTick) return std::nullopt;
        return BestLevel{best_, qty_[slot(best_)]};
    }

    size_t size() const noexcept { return live_;}
    void clear() noexcept {qty_.fill(Qty{0}); live_ = 0; best_ = kInvalidTick;}


    template<typename OutIt>
    size_t top_n (size_t n, OutIt out) const {
        if (best_ == kInvalidTick) return 0;

        size_t k = 0;
        const int64_t step = (S == Side::Buy) ? - 1 : +1;

        for (auto t = static_cast<int64_t>(best_); in_band(Ticks{t}) && k < n ; t += step) {
            const Qty q = qty_[slot(Ticks(t))];

            if (!(q == Qty{0})) { *out++ = BestLevel{Ticks{t}, q}; ++k;}

            return k;

        }
    }

    std::optional<BestLevel> scan_best_brutforce() const noexcept {
        std::optional<BestLevel> r;

        for (int64_t t = lo_; t < lo_ + int64_t(kBand); ++t) {
            const Qty q = qty_[slot(Ticks{t})];

            if (! (q == Qty{0}) && (!r || better(Ticks{t}, r->px)))
                r = BestLevel{Ticks{t}, q};
        }

        return r;
    }
};

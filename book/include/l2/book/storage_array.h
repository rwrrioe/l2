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
        return v >=lo_ && v < lo_ + int64_t(kBand);
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

        for (t += step; in_band(Ticks{t}); t += step) {
            if (!(qty_[slot(Ticks{t})] == Qty{0}) ) {best_ = Ticks{t}; return;}
        }
        best_ = kInvalidTick;   // live_ > 0, но всё живое вне band быть не может — защитный ход
    }

    void recenter(Ticks px) noexcept {
        const int64_t p = static_cast<int64_t>(px);
        const int64_t new_lo = p - int64_t(kBand / 2);

        if (new_lo >= lo_ + int64_t(kBand) || new_lo + int64_t(kBand) <= lo_) {
            clear();
            lo_ = new_lo;
            return;
        }

        const int64_t from = (new_lo > lo_) ? lo_ : new_lo + int64_t(kBand);
        const int64_t to = (new_lo > lo_) ? new_lo : lo_ + int64_t(kBand);

        bool best_lost = false;
        for (int64_t t = from; t < to; ++t) {
            Qty& c = qty_[slot(Ticks{t})];

            if (!(c == Qty{0})) {
                c = Qty{0};
                --live_;
                if (Ticks{t} == best_) best_lost = true;
            }
        }

        lo_ = new_lo;

        if (best_lost) {
            best_ = kInvalidTick;
            if (live_ != 0) {
                const int64_t start = (S == Side::Buy) ? lo_ + int64_t(kBand) - 1 : lo_;
                const int64_t step = (S == Side::Buy ) ? -1 : +1;

                for (int64_t t = start; in_band(Ticks{t}); t += step ) {
                    if(!(qty_[slot(Ticks{t})] == Qty{0})) {best_ = Ticks{t}; break;}
                }

            }
        }

    };

public:
    void update(Ticks px, Qty q) noexcept {
        if (!in_band(px)) {
            if (q == Qty{0}) return;
            // глубокий уровень за окном (хуже best) — не отслеживаем: окно
            // следует за вершиной книги, а recenter на далёкую цену выселил
            // бы всю вершину. Двигаем окно только когда рынок улучшается
            // за его край.
            if (!better(px, best_)) return;
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
        }

        return k;
    }

    std::optional<BestLevel> scan_best_bruteforce() const noexcept {
        std::optional<BestLevel> r;

        for (int64_t t = lo_; t < lo_ + int64_t(kBand); ++t) {
            const Qty q = qty_[slot(Ticks{t})];

            if (! (q == Qty{0}) && (!r || better(Ticks{t}, r->px)))
                r = BestLevel{Ticks{t}, q};
        }

        return r;
    }
};

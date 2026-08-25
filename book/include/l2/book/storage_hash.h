#pragma once
#include <l2/common/types.h>
#include <ankerl/unordered_dense.h>
#include <optional>

template<Side S>
class HashStorage {
    ankerl::unordered_dense::map<int64_t, Qty> levels_;
    Ticks best_ = kInvalidTick;

    static bool better(Ticks a, Ticks b) noexcept {
        if (b == kInvalidTick) return true;
        if constexpr (S == Side::Buy) return static_cast<int64_t>(a) > static_cast<int64_t>(b);
        else                          return static_cast<int64_t>(a) < static_cast<int64_t>(b);
    }
    void rescan_best() noexcept {
        for (auto& [t, q] : levels_)
            if (better(Ticks{t}, best_)) best_ = Ticks{t};
    }

public:
    void update(Ticks px, Qty q) noexcept {
        if (q == Qty{0}) {
            if (levels_.erase(static_cast<int64_t>(px)) && px == best_) rescan_best();
        } else {
            levels_[static_cast<int64_t>(px)] = q;
            if (better(px, best_)) best_ = px;
        }
    }
    std::optional<BestLevel> best() const noexcept {
        if (best_ == kInvalidTick) return std::nullopt;
        return BestLevel{best_, levels_.at(raw(best_))};
    }
    size_t size() const noexcept { return levels_.size(); }
    void   clear() noexcept { levels_.clear(); best_ = kInvalidTick; }

    // Мапа не упорядочена, а нужны n лучших: держим кучу из n кандидатов,
    // на вершине — ХУДШИЙ из отобранных (его выгоднее всего заменить).
    // Один проход по мапе, O(m log n) вместо полной сортировки O(m log m).
    template<typename OutIt>
    size_t top_n(size_t n, OutIt out) const {
        if (n == 0 || levels_.empty()) return 0;

        const size_t target_n = std::min(n, size_t{128});

        std::array<BestLevel, 128> buffer;
        size_t buffer_size = 0;

        // comp = better ⇒ std::*_heap строят кучу, где наверху элемент,
        // который не лучше никого — то есть худший из отобранных
        auto worst_on_top = [](const BestLevel& a, const BestLevel& b) noexcept -> bool {
            return better(a.px, b.px);
        };

        for (const auto& [t,q] : levels_) {
            const BestLevel level{Ticks{t}, q};

            if (buffer_size < target_n) {
                buffer[buffer_size++] = level;
                std::push_heap(buffer.begin(), buffer.begin() + buffer_size, worst_on_top);
            } else if (better(level.px, buffer[0].px)) {
                std::pop_heap(buffer.begin(), buffer.begin() + buffer_size, worst_on_top);
                buffer[buffer_size - 1] = level;
                std::push_heap(buffer.begin(), buffer.begin() + buffer_size, worst_on_top);
            }
        }

        // выдаём от лучшего к худшему — в том же порядке, что Map/Array top_n
        std::sort_heap(buffer.begin(), buffer.begin() + buffer_size, worst_on_top);
        std::copy(buffer.begin(), buffer.begin() + buffer_size, out);

        return buffer_size;
    }

    std::optional<BestLevel> scan_best_bruteforce() const noexcept;
};

template<Side S>
std::optional<BestLevel> HashStorage<S>::scan_best_bruteforce() const noexcept {
    std::optional<BestLevel> r;

    for (const auto& [t, q] : levels_)
        if (!r || better(Ticks{t}, r->px)) r = BestLevel{Ticks{t}, q};

    return r;
}

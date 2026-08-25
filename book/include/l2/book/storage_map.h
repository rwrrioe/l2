#pragma once
#include <functional>
#include <l2/common/types.h>
#include <map>
#include <optional>


template<Side S>
class MapStorage{
    using Cmp = std::conditional_t<S == Side::Buy,
                                std::greater<Ticks>, std::less<Ticks>>;
    std::map<Ticks, Qty, Cmp> levels_;
public:
    void update (Ticks px, Qty q) noexcept {
        if (q == Qty{0}) levels_.erase(px);
        else levels_[px] = q;
    }

    std::optional<BestLevel> best() const noexcept {
        if (levels_.empty()) return std::nullopt;

        auto it = levels_.begin();
        return BestLevel{it->first, it->second};
    }

    size_t size() const noexcept {return levels_.size();}
    void clear() noexcept {levels_.clear();}

    template<typename OutIt>
    size_t top_n (size_t n, OutIt out) const {
        size_t k = 0;

        for (auto it = levels_.begin(); it != levels_.end() && k < n; ++it, ++k) {
            *out++ = BestLevel{it->first, it->second};
        }

        return k;
    }

    std::optional<BestLevel> scan_best_bruteforce() const noexcept {return best();}
};

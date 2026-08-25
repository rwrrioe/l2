#pragma once
#include <l2/book/top_of_book.h>
#include <l2/common/types.h>

// Cont-Kukanov-Stoikov (2014)
class OfiCalculator {
    TopOfBook prev_{};
    bool has_prev_ = false;
public:
    double on_top_update(const TopOfBook& t) noexcept {
        if (!has_prev_ || !has_both_sides(t) ||!has_both_sides(prev_)) {
            prev_ = t; has_prev_ = true; return 0.0;
        }

        double e = 0.0;

        // bid: цена выросла -> вся новая ликвидность в плюс; цена упала ->
        // старая ликвидность ушла, в минус; цена та же -> срабатывают ОБА
        // условия и остаётся дельта объёма q_n - q_{n-1}
        if (raw(t.bid_px) >= raw(prev_.bid_px)) e += double(raw(t.bid_qty));
        if (raw(t.bid_px) <= raw(prev_.bid_px)) e -= double(raw(prev_.bid_qty));

        // ask зеркально и со знаком минус: рост ликвидности на офере — давление вниз
        if (raw(t.ask_px) <= raw(prev_.ask_px)) e -= double(raw(t.ask_qty));
        if (raw(t.ask_px) >= raw(prev_.ask_px)) e += double(raw(prev_.ask_qty));

        prev_ = t;
        return e;
    }

    void reset() noexcept {has_prev_ = false;}
};

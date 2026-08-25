#pragma once
#include <l2/book/top_of_book.h>


inline double weighted_mid(const TopOfBook& t) noexcept {
    const double qb = double(raw(t.bid_qty));
    const double qa = double(raw(t.ask_qty));

    const double I = qb / (qb+qa);

    return I * double(raw(t.ask_px)) + (1.0 - I) * double(raw(t.bid_px));
}


inline double mid(const TopOfBook& t) noexcept {
    return 0.5 * (double(raw(t.bid_px)) + double(raw(t.ask_px)));
}

#pragma once
#include <l2/book/top_of_book.h>
#include <cassert>
#include <l2/book/book.h>

template<typename BidS, typename AskS>
void BookL2<BidS,AskS>::check_invariants() const noexcept {
#if defined (L2_CHECK_INVARIANTS)
    if(has_both_sides(top_))
        assert(static_cast<int64_t>(top_.bid_px) < static_cast<int64_t>(top_.ask_px) && "crossed book");

    //check cache stale
    {
        const auto bb = bids_.scan_best_bruteforce();
        const auto cb = bids_.best();

        assert(bb.has_value() == cb.has_value());

        if(bb) assert(bb->px == cb->px && bb->qty == cb->qty);

        const auto ba = asks_.scan_best_bruteforce();
        const auto ca = asks_.best();

        assert(ba.has_value() == ca.has_value());

        if (ba) assert (ba->px == ca->px && ba->qty == ca->qty);
    }

#endif
}

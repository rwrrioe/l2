#include <l2/common/types.h>
#define XXH_STATIC_LINKING_ONLY
#include <l2/book/book.h>
#include <l2/book/storage_array.h>
#include <l2/book/storage_hash.h>
#include <l2/book/storage_map.h>
#include <l2/book/invariants.h>
#include <xxhash.h>

template<typename BidS, typename AskS>
uint64_t BookL2<BidS, AskS>::hash() const noexcept {
    const size_t kHashDepth = 32;

    BestLevel buf[kHashDepth];
    XXH64_state_t st;
    XXH64_reset(&st, 0);

    size_t n = bids_.top_n(kHashDepth, buf);
    XXH64_update(&st,buf, n * sizeof(BestLevel));

    n = asks_.top_n(kHashDepth, buf);
    XXH64_update(&st,buf, n * sizeof(BestLevel));

    return XXH64_digest(&st);
}

template<typename BidS, typename AskS>
size_t BookL2<BidS, AskS>::to_levels(Side s, size_t n, std::span<BestLevel>out) const {
    return (s == Side::Buy) ? bids_.top_n(n, out.begin())
                            : asks_.top_n(n, out.begin());
}

template class BookL2<MapStorage<Side::Buy>,   MapStorage<Side::Sell>>;
template class BookL2<ArrayStorage<Side::Buy>, ArrayStorage<Side::Sell>>;
template class BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;

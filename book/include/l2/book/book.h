#pragma once
#include <l2/book/top_of_book.h>
#include <l2/book/storage_map.h>
#include <l2/book/storage_hash.h>
#include <l2/book/storage_array.h>
#include <span>

template<typename BidS, typename AskS>
class BookL2 {
public:

    //full resync on new snapshot
    void apply_snapshot(std::span<const LevelUpdate> bids,
                        std::span<const LevelUpdate> asks) noexcept {
        bids_.clear(); asks_.clear();

        for (auto& u : bids) bids_.update(u.px, u.qty);
        for (auto& u : asks) asks_.update(u.px, u.qty);

        refresh_top();
        check_invariants();
    }

    //delta, input already validated by sequencer
    void apply (std::span<const LevelUpdate> bids,
                std::span<const LevelUpdate> asks) noexcept {
        for (auto& u : bids) bids_.update(u.px, u.qty);
        for(auto& u : asks) asks_.update(u.px, u.qty);

        refresh_top_and_bump();
        check_invariants();
    }

    const TopOfBook& top() const noexcept {return top_;}
    uint64_t hash() const noexcept;

    size_t to_levels (Side s, size_t n,
                    std::span<BestLevel> out) const;


    void clear() noexcept {bids_.clear(); asks_.clear(); top_ = {};}

private:
    BidS bids_;
    AskS asks_;
    TopOfBook top_;

    void refresh_top() noexcept {
        const auto b = bids_.best();
        const auto a = asks_.best();

        top_.bid_px = b ? b->px: kInvalidTick;
        top_.bid_qty = b ? b->qty : Qty{0};

        top_.ask_px = a ? a->px : kInvalidTick;
        top_.ask_qty = a ? a->qty : Qty{0};
    }

    void refresh_top_and_bump() noexcept {refresh_top(); ++top_.seq;}
    void check_invariants() const noexcept;
};

template<Side> class MapStorage; template<Side> class ArrayStorage; template<Side> class HashStorage;

using BookMap = BookL2<MapStorage<Side::Buy>, MapStorage<Side::Sell>>;
using BookArray = BookL2<ArrayStorage<Side::Buy>,ArrayStorage<Side::Sell>>;
using BookHash =  BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;


extern template class BookL2<MapStorage<Side::Buy>, MapStorage<Side::Sell>>;
extern template class BookL2<ArrayStorage<Side::Buy>, ArrayStorage<Side::Sell>>;
extern template class BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;

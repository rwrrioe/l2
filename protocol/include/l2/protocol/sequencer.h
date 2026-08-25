#pragma once

#include <l2/common/types.h>
#include <cstdint>
#include <l2/protocol/events.h>
enum class SeqState : uint8_t { Disconnected,Buffering, FirstLive ,Synced, Desynced };
enum class SeqAction : uint8_t { Apply, ApplySnapshot ,Drop, RequestSnapshot, Resync };

struct SeqStats {
    uint64_t
    gaps = 0,
    dropped = 0,
    resyncs = 0,
    buffered = 0,
    buffer_overflows = 0;
};

class TailView {
    const EventBuffer* buf_ = nullptr;
    size_t first_ = 0, count_ = 0;
public:
    TailView() = default;
    TailView(const EventBuffer& b, size_t first, size_t count) noexcept
    : buf_(&b), first_(first), count_(count) {}

    DepthEvent operator[](size_t k) const noexcept {return buf_->view(first_+k);}

    size_t size() const noexcept { return count_;}
    bool empty() const noexcept {return count_ == 0;}

};

struct SnapshotAction {
    SeqAction action;
    TailView tail;
};

template<typename Policy>
class Sequencer {
    SeqState state_ = SeqState::Disconnected;
    uint64_t prev_u_ = 0;
    EventBuffer buffer_;
    SeqStats stats_;
    TailView tail;
public:
    SeqAction on_connected() noexcept {
        state_ = SeqState::Buffering;
        buffer_.clear();
        return SeqAction::RequestSnapshot;
    }

    SeqAction on_event(const DepthEvent& e) noexcept {
        switch (state_) {
        case SeqState::FirstLive:
            if (Policy::is_stale(e, prev_u_)) {
                ++stats_.dropped;
                return SeqAction::Drop;
            }
            if (!Policy::is_first_applicable(e, prev_u_)) {
                ++stats_.gaps;
                state_ = SeqState::Desynced;
                return SeqAction::Resync;
            }

            prev_u_ = e.u;
            state_ = SeqState::Synced;
            return SeqAction::Apply;

        case SeqState::Synced:
            if (Policy::is_contigous(e, prev_u_)) {
                prev_u_ = e.u;
                return SeqAction::Apply;
            }
            ++stats_.gaps;
            state_ = SeqState::Desynced;
            return SeqAction::Resync;

        case SeqState::Buffering:
            if(buffer_.push(e)) {
                ++stats_.buffered;
                return SeqAction::Drop;
            }

            ++stats_.buffer_overflows;
            state_ = SeqState::Desynced;
            return SeqAction::Resync;

        case SeqState::Disconnected:
        case SeqState::Desynced:
            ++stats_.dropped;
            return SeqAction::Drop;
        }


        return SeqAction::Drop;
    };

   SnapshotAction on_snapshot(const SnapshotEvent& s) noexcept {
        if (state_ != SeqState::Buffering) {return SnapshotAction{SeqAction::Drop, {}};}

        //drop stale
        size_t i = 0;
        while (i < buffer_.size() && Policy::is_stale(buffer_.view(i), s.last_update_id))
            ++i;

        //check if buffer is full stale
        if (i == buffer_.size()) {
            state_ = SeqState::FirstLive;
            prev_u_ = s.last_update_id;
            return SnapshotAction{SeqAction::ApplySnapshot, {}};
        }

        if (!Policy::is_first_applicable(buffer_.view(i), s.last_update_id)) {
            buffer_.clear();
            state_ = SeqState::Desynced;
            ++stats_.resyncs;
            return SnapshotAction{SeqAction::Resync, {}};
        }

        //apply
        uint64_t u = buffer_.view(i).u;

        for (size_t j = i + 1; j < buffer_.size(); ++j) {
            const DepthEvent ev = buffer_.view(j);

            if (!Policy::is_contigous(ev, u)) {
                buffer_.clear();
                state_ = SeqState::Desynced;
                ++stats_.gaps; ++stats_.resyncs;
                return SnapshotAction{SeqAction::Resync, {}};
            }

            u = ev.u;
        }

        //commit seqstate
        prev_u_ = u;
        state_ = SeqState::Synced;
        return {SeqAction::ApplySnapshot, TailView{buffer_, i, buffer_.size() - i}};
    }


     SeqAction on_disconnect() noexcept {
         state_ = SeqState::Disconnected;
         buffer_.clear();
         return SeqAction::Drop;
     };
     SeqAction  on_book_invalid() noexcept {
         state_ = SeqState::Desynced;
         ++stats_.resyncs;
         return SeqAction::Resync;
     };

     SeqState  state() const noexcept {return state_;}
     const SeqStats& stats() const noexcept {return stats_;}
};

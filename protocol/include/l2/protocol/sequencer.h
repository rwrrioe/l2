#include <cstdint>
#include <l2/protocol/events.h>
enum class State { Disconnected, Buffering, Synced, Desynced };
enum class Action { Apply, Drop, RequestSnapshot, Resync };

template<typename Policy>
class Sequencer {
    State state_ = State::Disconnected;
    uint64_t prev_u_ = 0;
    EventBuffer buffer_;

public:
    Action on_conncted();
    Action on_event();
    Action on_snapshot();
    Action on_disconnect();
    Action on_book_invalid();

    State state() const noexcept {return state_;}
};

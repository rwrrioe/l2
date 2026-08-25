#pragma once
#include <l2/protocol/events.h>


struct SpotPolicy {
    static bool is_stale(const DepthEvent& e, uint64_t last_updated_id) noexcept {
        return e.u <= last_updated_id;
    }

    static bool is_first_applicable(const DepthEvent& e, uint64_t last_updated_id) noexcept {
        return e.U <= last_updated_id +1 && last_updated_id + 1 <= e.u;
    }

    static bool is_contigous(const DepthEvent& e, uint64_t prev_u) noexcept {
        return e.U == prev_u + 1;
    }
};

struct FuturesPolicy {
 static bool is_stale (const DepthEvent&e, uint64_t last_update_id) noexcept {
     return e.u < last_update_id;
 }

 static bool is_first_applicable(const DepthEvent& e, uint64_t last_update_id) noexcept {
     return e.U <= last_update_id && last_update_id <= e.u;
 }

 static bool is_contigous(const DepthEvent& e, uint64_t prev_u) noexcept {
     return e.pu == prev_u;
 }

};

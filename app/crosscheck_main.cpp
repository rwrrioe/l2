#include <l2/transport/file_source.h>
#include <l2/protocol/parser.h>
#include <l2/protocol/sequencer.h>
#include <l2/protocol/venue.h>
#include <l2/book/book.h>
#include <l2/book/storage_map.h>
#include <l2/book/storage_array.h>
#include <l2/book/storage_hash.h>

#include <cstdio>
#include <cstdlib>
#include <variant>

//three storages differential live-test
int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: crosscheck <frames.bin> <tick_fp>\n");
        return 1;
    }

    const TickConverter conv{std::strtoull(argv[2], nullptr, 10), 1};
    FileSource src{argv[1]};
    Parser parser{conv};
    Sequencer<SpotPolicy> seq;

    BookMap   bm;
    BookArray ba;
    BookHash  bh;

    uint64_t applied = 0, snaps = 0;

    const auto check = [&](uint64_t u, const char* what) -> bool {
        const uint64_t hm = bm.hash(), ha = ba.hash(), hh = bh.hash();
        if (hm == ha && hm == hh) return true;
        std::fprintf(stderr,
                     "MISMATCH after %s u=%llu (applied=%llu)\n"
                     "  map   %016llx\n  array %016llx\n  hash  %016llx\n",
                     what, static_cast<unsigned long long>(u), static_cast<unsigned long long>(applied),
                     static_cast<unsigned long long>(hm), static_cast<unsigned long long>(ha),
                     static_cast<unsigned long long>(hh));
        return false;
    };

    Frame f;
    while (src.next(f)) {
        if (f.kind == StreamKind::Connected)    { seq.on_connected(); continue; }
        if (f.kind == StreamKind::Disconnected) {
            seq.on_disconnect();
            bm.clear(); ba.clear(); bh.clear();
            continue;
        }

        auto r = parser.parse(f);
        if (!r) continue;

        if (auto* d = std::get_if<DepthEvent>(&r.value())) {
            if (seq.on_event(*d) != SeqAction::Apply) continue;
            bm.apply(d->bids, d->asks);
            ba.apply(d->bids, d->asks);
            bh.apply(d->bids, d->asks);
            ++applied;
            if (!check(d->u, "event")) return 1;
        } else if (auto* s = std::get_if<SnapshotEvent>(&r.value())) {
            const auto res = seq.on_snapshot(*s);
            if (res.action != SeqAction::ApplySnapshot) continue;

            bm.apply_snapshot(s->bids, s->asks);
            ba.apply_snapshot(s->bids, s->asks);
            bh.apply_snapshot(s->bids, s->asks);
            ++snaps;
            if (!check(s->last_update_id, "snapshot")) return 1;

            for (size_t k = 0; k < res.tail.size(); ++k) {
                const DepthEvent e = res.tail[k];
                bm.apply(e.bids, e.asks);
                ba.apply(e.bids, e.asks);
                bh.apply(e.bids, e.asks);
                ++applied;
                if (!check(e.u, "tail event")) return 1;
            }
        }
    }

    std::printf("OK: %llu events, %llu snapshots, all hashes agree\n",
                static_cast<unsigned long long>(applied), static_cast<unsigned long long>(snaps));
    return 0;
}

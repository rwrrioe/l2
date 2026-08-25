#include <l2/transport/file_source.h>
#include <l2/protocol/parser.h>
#include <l2/protocol/sequencer.h>
#include <l2/protocol/venue.h>
#include <l2/book/book.h>
#include <l2/book/storage_array.h>
#include <l2/signals/ofi.h>
#include <l2/signals/microprice.h>
#include <cstdio>

//replay from record_main.cpp's data
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: replay <frames.bin> <tick_fp> <hash.log>\n"); return 1;}

    TickConverter conv{std::strtoull(argv[2], nullptr, 10), 1};
    FileSource src{argv[1]};
    Parser parser{conv};
    Sequencer<SpotPolicy> seq;
    BookArray book;
    OfiCalculator ofi;
    std::FILE* hlog = std::fopen(argv[3], "w");

    auto apply_depth = [&](const DepthEvent& e) {
        book.apply(e.bids, e.asks);
        const double en = ofi.on_top_update(book.top());
        std::fprintf(hlog, "%llu %016llx %.1f %.2f\n",
            static_cast<unsigned long long>(e.u),
            static_cast<unsigned long long>(book.hash()),
            en, weighted_mid(book.top()));
    };

    seq.on_connected();

    Frame f;
    while (src.next(f)) {
        // контрольные кадры из журнала: replay воспроизводит реконнекты (В-16)
        if (f.kind == StreamKind::Connected)    { seq.on_connected(); continue; }
        if (f.kind == StreamKind::Disconnected) { seq.on_disconnect(); ofi.reset(); continue; }

        auto r = parser.parse(f);
        if (!r) continue;

        if (auto* d = std::get_if<DepthEvent>(&r.value())) {
            switch(seq.on_event(*d)) {
                case SeqAction::Apply: apply_depth(*d); break;
                case SeqAction::Resync: break;
                default: break;
            }
        } else if (auto *s = std::get_if<SnapshotEvent>(&r.value())) {
            const auto res = seq.on_snapshot(*s);

            if (res.action == SeqAction::ApplySnapshot) {
                //snapshot first
                book.apply_snapshot(s->bids, s->asks);
                std::fprintf(hlog, "SNAP %llu %016llx\n",
                    static_cast<unsigned long long>(s->last_update_id),
                   static_cast<unsigned long long>(book.hash()));

                for (size_t k = 0; k < res.tail.size(); ++k) {
                    apply_depth(res.tail[k]);
                }
            }
        }
    }

    std::fclose(hlog);
    return 0;
}

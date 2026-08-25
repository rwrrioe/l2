#include <l2/transport/ws_source.h>
#include <l2/transport/rest_client.h>
#include <l2/protocol/parser.h>
#include <l2/protocol/sequencer.h>
#include <l2/protocol/venue.h>
#include <l2/book/book.h>
#include <l2/book/storage_array.h>
#include <l2/signals/ofi.h>
#include <l2/signals/microprice.h>
#include <l2/common/clock.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: live <symbol> <tick_fp>\n");
        return 1;
    }

    std::string sym_lo = argv[1], sym_up = argv[1];
    for (auto& c : sym_lo) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : sym_up) c = char(std::toupper(static_cast<unsigned char>(c)));

    const uint64_t tick_fp = std::strtoull(argv[2], nullptr, 10);
    const TickConverter conv{tick_fp, 1};

    const std::string stream_target = "/stream?streams=" + sym_lo + "@depth@100ms";
    const std::string snap_target   = "/api/v3/depth?symbol=" + sym_up + "&limit=1000";

    WSSource   ws{{"stream.binance.com", "9443", stream_target.c_str()}};
    RestClient rest{{"api.binance.com", "443"}};

    Parser parser{conv};
    Sequencer<SpotPolicy> seq;
    BookArray book;
    OfiCalculator ofi;

    const auto px = [&](Ticks t) { return double(raw(t)) * double(tick_fp) / 1e8; };

    // Ресинк: Buffering -> REST-снапшот -> сшивка. В однопоточном цикле между
    // on_connected и on_snapshot буфер пуст (мы блокируемся на REST и не читаем
    // ws), так что сшивка всегда идёт по ветке "все stale" -> FirstLive.
    const auto resync = [&]() -> bool {
        std::string body;
        for (int attempt = 0; attempt < 5; ++attempt) {
            seq.on_connected();
            if (!rest.get_with_backoff(snap_target.c_str(), body)) continue;

            const Frame sf{
                {reinterpret_cast<const std::byte*>(body.data()), body.size()},
                now_ns(),
                StreamKind::Snapshot,
            };

            auto r = parser.parse(sf);
            if (!r) continue;
            auto* s = std::get_if<SnapshotEvent>(&r.value());
            if (!s) continue;

            const auto res = seq.on_snapshot(*s);
            if (res.action != SeqAction::ApplySnapshot) continue;

            book.apply_snapshot(s->bids, s->asks);
            ofi.reset();
            for (size_t k = 0; k < res.tail.size(); ++k) {
                const DepthEvent e = res.tail[k];
                book.apply(e.bids, e.asks);
            }
            std::fprintf(stderr, "synced @ %llu\n",
                         static_cast<unsigned long long>(s->last_update_id));
            return true;
        }
        return false;
    };

    uint64_t applied = 0;
    Frame f;

    while (ws.next(f)) {
        if (f.kind == StreamKind::Connected)    { resync(); continue; }
        if (f.kind == StreamKind::Disconnected) {
            seq.on_disconnect();
            book.clear();
            ofi.reset();
            continue;
        }
        if (f.kind != StreamKind::Depth) continue;

        auto r = parser.parse(f);
        if (!r) continue;
        auto* d = std::get_if<DepthEvent>(&r.value());
        if (!d) continue;

        switch (seq.on_event(*d)) {
        case SeqAction::Apply: {
            book.apply(d->bids, d->asks);
            const double e_n  = ofi.on_top_update(book.top());
            const double wmid = weighted_mid(book.top());

            if (++applied % 50 == 0) {
                const auto& t = book.top();
                std::printf("bid %.2f x %llu | ask %.2f x %llu | wmid %.4f | ofi %+.1f\n",
                            px(t.bid_px), static_cast<unsigned long long>(raw(t.bid_qty)),
                            px(t.ask_px), static_cast<unsigned long long>(raw(t.ask_qty)),
                            wmid * double(tick_fp) / 1e8, e_n);
                std::fflush(stdout);
            }
            break;
        }
        case SeqAction::Resync:
            std::fprintf(stderr, "gap detected (gaps=%llu), resyncing\n",
                         static_cast<unsigned long long>(seq.stats().gaps));
            resync();
            break;
        default:
            break;
        }
    }

    return 0;
}

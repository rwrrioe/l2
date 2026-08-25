#include <l2/transport/ws_source.h>
#include <l2/transport/rest_client.h>
#include <l2/transport/recorder.h>
#include <l2/common/clock.h>
#include <l2/common/types.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

//write raw listing without parsing
int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: record <out.bin> <symbol> <seconds>\n");
        return 1;
    }

    std::string sym_lo = argv[2], sym_up = argv[2];
    for (auto& c : sym_lo) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : sym_up) c = char(std::toupper(static_cast<unsigned char>(c)));

    const int64_t deadline_ns = mono_ns() + std::atoll(argv[3]) * 1'000'000'000ll;

    const std::string stream_target = "/stream?streams=" + sym_lo + "@depth@100ms";
    // limit=50: снапшот должен влезать в 4К-слот Recorder::Rec (В-15)
    const std::string snap_target   = "/api/v3/depth?symbol=" + sym_up + "&limit=50";

    WSSource   ws{{"stream.binance.com", "9443", stream_target.c_str()}};
    RestClient rest{{"api.binance.com", "443"}};
    Recorder   rec{argv[1]};

    uint64_t frames = 0, snaps = 0;
    std::string body;
    Frame f;

    while (mono_ns() < deadline_ns && ws.next(f)) {
        rec.tap(f);   // и данные, и Connected/Disconnected — всё в журнал
        ++frames;

        // (ре)коннект -> протокол требует свежий снапшот; пишем его тем же
        // каналом кадров, чтобы replay прошёл тот же путь, что и live
        if (f.kind == StreamKind::Connected) {
            if (rest.get_with_backoff(snap_target.c_str(), body)) {
                rec.tap(Frame{
                    {reinterpret_cast<const std::byte*>(body.data()), body.size()},
                    now_ns(),
                    StreamKind::Snapshot,
                });
                ++snaps;
            } else {
                std::fprintf(stderr, "snapshot fetch failed, journal has no stitch point\n");
            }
        }
    }

    std::fprintf(stderr, "recorded %llu frames (%llu snapshots), journal drops: %llu\n",
                 static_cast<unsigned long long>(frames), static_cast<unsigned long long>(snaps),
                 static_cast<unsigned long long>(rec.dropped()));
    return 0;
}

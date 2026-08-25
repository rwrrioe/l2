#include <l2/transport/recorder.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

Recorder::Recorder(const char* path) {
    fd_ = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) throw std::runtime_error("recorder: open failed");

    writer_ = std::thread(&Recorder::writer_loop, this);
}

Recorder::~Recorder() {
    stop_.store(true, std::memory_order_release);
    if (writer_.joinable()) writer_.join();   // writer_loop дописывает хвост очереди
    if (fd_ >= 0) ::close(fd_);
}

void Recorder::tap(const Frame& f) noexcept {
    // Слот фиксированного размера (В-15): кадр больше 4К в журнал не влезает.
    // Потеря кадра ЖУРНАЛА, не конвейера — считаем громко, но не блокируемся.
    if (f.payload.size() > sizeof(Rec::payload)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Rec r;
    r.ts   = int64_t(f.rx_timestamp_ns);
    r.len  = uint32_t(f.payload.size());
    r.kind = uint8_t(f.kind);
    std::memcpy(r.payload, f.payload.data(), r.len);

    // try_push, не push: IO-поток НИКОГДА не ждёт диск. Очередь полна
    // (fsync-спайк, медленный диск) -> кадр журнала теряется, поток живёт.
    if (!q_.try_push(r)) dropped_.fetch_add(1, std::memory_order_relaxed);
}

void Recorder::writer_loop() {
    constexpr size_t kHdr = 4 + 8 + 1;   // формат FileSource: [len u32][ts u64][kind u8]

    // батчинг: копим ~1МиБ и пишем одним write() — один сисколл
    // на сотни кадров вместо сисколла на кадр
    std::vector<std::byte> out;
    out.reserve(1 << 20);

    const auto flush = [&] {
        size_t done = 0;
        while (done < out.size()) {
            const ssize_t n = ::write(fd_, out.data() + done, out.size() - done);
            if (n <= 0) break;   // диск отказал; кадры уже скопированы — теряем молча тут,
            done += size_t(n);   // но dropped_ на этот случай не заводим: это иной класс аварии
        }
        out.clear();
    };

    for (;;) {
        Rec* r = q_.front();
        if (!r) {
            flush();   // очередь пуста — самое время сбросить батч на диск
            if (stop_.load(std::memory_order_acquire) && !q_.front()) break;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }

        const size_t need = kHdr + r->len;
        if (out.size() + need > out.capacity()) flush();

        const size_t base = out.size();
        out.resize(base + need);
        std::memcpy(out.data() + base,        &r->len,  4);
        std::memcpy(out.data() + base + 4,    &r->ts,   8);
        std::memcpy(out.data() + base + 12,   &r->kind, 1);
        std::memcpy(out.data() + base + kHdr, r->payload, r->len);

        q_.pop();
    }

    flush();
}

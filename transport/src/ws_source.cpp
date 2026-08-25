#include <l2/transport/ws_source.h>
#include <l2/common/clock.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
namespace ssl       = net::ssl;
using tcp           = net::ip::tcp;

struct WSSource::Impl {
    WSConfig cfg;
    net::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};

    // optional: реконнект = уничтожить стрим целиком и построить заново;
    // beast-стримы не рассчитаны на повторный handshake после ошибки
    std::optional<websocket::stream<ssl::stream<tcp::socket>>> ws;
    beast::flat_buffer rdbuf;

    // копия сообщения: Frame::payload живёт до следующего next(),
    // rdbuf же переиспользуется — тот же контракт, что у EventBuffer::view
    std::vector<std::byte> payload;

    int64_t connected_at_ns = 0;
    int backoff_ms = 100;
    std::minstd_rand rng{std::random_device{}()};

    explicit Impl(WSConfig c) : cfg(c) {
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);
    }

    bool try_connect() noexcept {
        try {
            ws.emplace(ioc, ctx);

            tcp::resolver resolver{ioc};
            const auto results = resolver.resolve(cfg.host, cfg.port);

            if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), cfg.host))
                return false;

            net::connect(ws->next_layer().next_layer(), results);
            ws->next_layer().handshake(ssl::stream_base::client);

            // Host-заголовок: нестандартный порт обязан быть в нём явно
            std::string host_hdr = cfg.host;
            if (std::strcmp(cfg.port, "443") != 0) {
                host_hdr += ':';
                host_hdr += cfg.port;
            }
            ws->handshake(host_hdr, cfg.target);

            connected_at_ns = mono_ns();
            backoff_ms = 100;
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ws connect failed: %s\n", e.what());
            ws.reset();
            return false;
        }
    }

    void teardown() noexcept {
        // без вежливого close(): соединение уже мертво или устарело,
        // блокирующее прощание может висеть до TCP-таймаута
        try { ws.reset(); } catch (...) {}
    }

    static StreamKind classify(std::string_view p) noexcept {
        if (p.find("@depth") != std::string_view::npos) return StreamKind::Depth;
        if (p.find("@trade") != std::string_view::npos) return StreamKind::Trade;
        return StreamKind::Unknown;
    }

    bool next(Frame& out) noexcept {
        for (;;) {
            if (!ws) {
                if (!try_connect()) {
                    const int jitter = int(rng() % uint32_t(backoff_ms / 2 + 1));
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms + jitter));
                    backoff_ms = std::min(backoff_ms * 2, 10'000);
                    continue;
                }

                out = Frame{{}, now_ns(), StreamKind::Connected};
                return true;
            }

          //binance disconnects every 24h, leave before
            if (mono_ns() - connected_at_ns > cfg.max_conn_age_ns) {
                teardown();
                out = Frame{{}, now_ns(), StreamKind::Disconnected};
                return true;
            }

            boost::system::error_code ec;
            rdbuf.clear();
            ws->read(rdbuf, ec);
            const uint64_t rx_ns = now_ns();

            if (ec) {
                std::fprintf(stderr, "ws read failed: %s\n", ec.message().c_str());
                teardown();
                out = Frame{{}, rx_ns, StreamKind::Disconnected};
                return true;
            }

            const auto* data = static_cast<const std::byte*>(rdbuf.cdata().data());
            payload.assign(data, data + rdbuf.size());

            const std::string_view sv{reinterpret_cast<const char*>(payload.data()),
                                      payload.size()};

            out = Frame{{payload.data(), payload.size()}, rx_ns, classify(sv)};
            return true;
        }
    }
};

WSSource::WSSource(WSConfig cfg) : impl_(std::make_unique<Impl>(cfg)) {}
WSSource::~WSSource() = default;

bool WSSource::next(Frame& out) {
    return impl_->next(out);
}

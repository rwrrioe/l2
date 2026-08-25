#include <l2/transport/rest_client.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace ssl   = net::ssl;
using tcp       = net::ip::tcp;

struct RestClient::Impl {
    RestConfig cfg;
    net::io_context ioc;
    ssl::context ctx{ssl::context::tls_client};
    std::minstd_rand rng{std::random_device{}()};

    explicit Impl(RestConfig c) : cfg(c) {
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);
    }

    int get(const char* target, std::string& out) noexcept {
        try {
            tcp::resolver resolver{ioc};
            ssl::stream<tcp::socket> stream{ioc, ctx};

            // SNI: без него сервер за общим IP не знает, чей сертификат отдавать
            if (!SSL_set_tlsext_host_name(stream.native_handle(), cfg.host))
                return 0;

            net::connect(stream.next_layer(), resolver.resolve(cfg.host, cfg.port));
            stream.handshake(ssl::stream_base::client);

            http::request<http::empty_body> req{http::verb::get, target, 11};
            req.set(http::field::host, cfg.host);
            req.set(http::field::user_agent, "l2-engine/0.1");
            http::write(stream, req);

            beast::flat_buffer buf;
            http::response<http::string_body> resp;
            http::read(stream, buf, resp);

            // серверы часто рвут TLS без close_notify — это не ошибка ответа
            boost::system::error_code ec;
            stream.shutdown(ec);

            out = std::move(resp.body());
            return int(resp.result_int());
        } catch (...) {
            return 0;
        }
    }
};

RestClient::RestClient(RestConfig cfg) : impl_(std::make_unique<Impl>(cfg)) {}
RestClient::~RestClient() = default;
RestClient::RestClient(RestClient&&) noexcept = default;
RestClient& RestClient::operator=(RestClient&&) noexcept = default;

int RestClient::get(const char* target, std::string& out) noexcept {
    return impl_->get(target, out);
}

bool RestClient::get_with_backoff(const char* target, std::string& out) noexcept {
    int backoff_ms = impl_->cfg.base_backoff_ms;

    for (int attempt = 0; attempt < impl_->cfg.max_attempts; ++attempt) {
        out.clear();
        const int status = impl_->get(target, out);
        if (status == 200) return true;

        // джиттер разводит клиентов по времени: без него после сбоя все
        // ретраят синхронно и снова кладут сервер (thundering herd)
        const int jitter = int(impl_->rng() % uint32_t(backoff_ms / 2 + 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms + jitter));
        backoff_ms = std::min(backoff_ms * 2, 8000);
    }

    return false;
}

#pragma once
#include <memory>
#include <string>

struct RestConfig {
    const char* host;            // api.binance.com
    const char* port = "443";
    int max_attempts = 5;
    int base_backoff_ms = 250;   // растёт x2 до 8с, с джиттером
};

class RestClient {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit RestClient(RestConfig cfg);
    ~RestClient();

    RestClient(RestClient&&) noexcept;
    RestClient& operator=(RestClient&&) noexcept;

    RestClient(const RestClient&) = delete;
    RestClient& operator=(const RestClient&) = delete;

    int get(const char* target, std::string& out) noexcept;

    bool get_with_backoff(const char* target, std::string& out) noexcept;
};

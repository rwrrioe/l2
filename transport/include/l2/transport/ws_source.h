#pragma once
#include <l2/transport/frame_source.h>
#include <memory>

struct WSConfig {
    const char* host;
    const char* port;
    const char* target;
    int64_t max_conn_age_ns = 23ll * 3600 * 1'000'000'000;
};

class WSSource final : public IFrameSource {
    struct Impl;
    std::unique_ptr <Impl> impl_;
public:
    explicit WSSource (WSConfig cfg);
    ~WSSource() override;

    bool next(Frame& out) override;
};

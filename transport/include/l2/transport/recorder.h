#pragma once
#include <l2/common/types.h>
#include <l2/queue/spsc.h>
#include <thread>
#include <atomic>

class Recorder {
  struct Rec {
      int64_t ts; uint32_t len; uint8_t kind;
      std::byte payload[4096];
  };

  SPSQ<Rec> q_ {4096};
  std::thread writer_;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> dropped_{0};
  int fd_;

  void writer_loop();
public:
    explicit Recorder(const char* path);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    void tap(const Frame& f) noexcept;
    uint64_t dropped() const noexcept {return dropped_.load(std::memory_order_relaxed);}

};

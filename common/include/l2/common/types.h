#pragma once

#include <cstdint>
#include <span>

enum class Ticks : int64_t {};
enum class Qty : uint64_t {};
enum class OrderId : uint64_t {};
enum class Seq : uint64_t {};
enum class SymbolId : uint16_t{};

enum class Side : uint8_t {Buy = 0, Sell = 1};

constexpr Ticks kInvalidTick{INT64_MIN};

constexpr Ticks next_tick(Ticks t ) noexcept {return Ticks{static_cast<int64_t>(t) + 1};}
constexpr Ticks prev_tick (Ticks t) noexcept {return Ticks{static_cast<int64_t>(t) - 1};}
constexpr bool  operator<(Ticks a, Ticks b) noexcept { return static_cast<uint64_t>(a) < static_cast<uint64_t>(b); }
constexpr bool  operator==(Ticks a, Ticks b) noexcept { return static_cast<uint64_t>(a) == static_cast<uint64_t>(b); }
constexpr bool  operator==(Qty a, Qty b) noexcept { return static_cast<uint64_t>(a) == static_cast<uint64_t>(b); }

inline constexpr uint64_t kFpScale = 100'000'000ull;

struct BestLevel {Ticks px; Qty qty;};

template<typename E>
constexpr auto raw(E e) noexcept {return static_cast<std::underlying_type_t<E>>(e);}


class TickConverter {
    uint64_t tick_size_fp_;
    uint64_t step_size_fp_;
public:
    constexpr TickConverter(uint64_t tick_fp, uint64_t step_fp) noexcept
    : tick_size_fp_(tick_fp), step_size_fp_(step_fp) {}

    Ticks to_ticks(uint64_t price_fp) const noexcept {
        return static_cast<Ticks>(price_fp / tick_size_fp_);
    }

    uint64_t to_fp (Ticks t) const noexcept {
        return static_cast<uint64_t>(t) * tick_size_fp_;
    }

    bool is_valid (uint64_t price_fp) const noexcept {
        return price_fp % tick_size_fp_ == 0;
    }
};

struct LevelUpdate {
    Ticks px;
    Qty qty;
};

// Connected/Disconnected — синтетические кадры от WS-источника: события
// соединения едут тем же каналом, что и данные, и попадают в журнал,
// поэтому replay воспроизводит реконнекты без спецслучаев (В-16, вариант "а").
enum class StreamKind : uint8_t {Depth = 0, Snapshot = 1, Trade = 2, Unknown = 3,
                                 Connected = 4, Disconnected = 5};

struct Frame {
    std::span<const std::byte> payload;
    uint64_t rx_timestamp_ns;
    StreamKind kind;
};

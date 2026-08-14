# L2 Engine — полный код с разъяснениями

Рабочий документ для разбора. Правила работы с ним:

1. **Файлы помечены ролями.**
   - 🔧 **ПЕРИФЕРИЯ** — читаете, понимаете, можно брать как есть (это делегируемая зона по нашему соглашению).
   - ✍️ **ЯДРО** — разбираете построчно, закрываете документ, пишете руками. Это ваш красный список: время жизни, шаблоны, инварианты.
2. **Блоки `❓ В-N`** — стоп-вопросы. Встретили — остановитесь и ответьте себе письменно, прежде чем читать дальше. Ответов в документе нет.
3. **Что намеренно отсутствует:** матчинг-движок (недели 3–4, отдельный разбор), `bench/harness.cpp` (появится после РАЗБОР 5–6), полный TLS-тюнинг в `ws_source`.
4. **РАЗБОР 1–4 из tests/bench всё ещё за вами** — в конце документа они переформулированы вместе с 5–6.

Порядок изучения = порядок файлов в документе. Он совпадает с этапами 1–8 нашей таблицы реализации.

---

# Модуль `common/` — словарь

## `common/include/l2/common/types.h` — ✍️ ЯДРО

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

namespace l2 {

// ── Сильные типы ────────────────────────────────────────────
// enum class => нет неявных конверсий, компилятор ловит
// перестановку аргументов. Кодогенерация идентична int64_t.
enum class Ticks    : int64_t  {};
enum class Qty      : uint64_t {};
enum class OrderId  : uint64_t {};
enum class Seq      : uint64_t {};
enum class SymbolId : uint16_t {};

enum class Side : uint8_t { Buy = 0, Sell = 1 };

// Хелпер вместо std::to_underlying (C++23)
template<typename E>
constexpr auto raw(E e) noexcept { return static_cast<std::underlying_type_t<E>>(e); }

constexpr Ticks kInvalidTick{INT64_MIN};

// Арифметика тиков — только явная, только именованная.
constexpr Ticks next_tick(Ticks t) noexcept { return Ticks{raw(t) + 1}; }
constexpr Ticks prev_tick(Ticks t) noexcept { return Ticks{raw(t) - 1}; }
constexpr bool  operator<(Ticks a, Ticks b) noexcept { return raw(a) < raw(b); }
constexpr bool  operator==(Ticks a, Ticks b) noexcept { return raw(a) == raw(b); }
constexpr bool  operator==(Qty a, Qty b) noexcept { return raw(a) == raw(b); }

// ── Fixed-point ─────────────────────────────────────────────
// Цены/количества в проекте: uint64_t = value * 1e8 (как в leapfirst).
inline constexpr uint64_t kFpScale = 100'000'000ull;

// ── TickConverter ───────────────────────────────────────────
// Создаётся из exchangeInfo при старте. Единственное место,
// где fixed-point цена превращается в тик и обратно.
class TickConverter {
    uint64_t tick_size_fp_;
    uint64_t step_size_fp_;
public:
    constexpr TickConverter(uint64_t tick_fp, uint64_t step_fp) noexcept
        : tick_size_fp_(tick_fp), step_size_fp_(step_fp) {}

    constexpr bool is_valid_price(uint64_t px_fp) const noexcept {
        return px_fp % tick_size_fp_ == 0;
    }
    constexpr Ticks to_ticks(uint64_t px_fp) const noexcept {
        return Ticks{static_cast<int64_t>(px_fp / tick_size_fp_)};
    }
    constexpr uint64_t to_fp(Ticks t) const noexcept {
        return static_cast<uint64_t>(raw(t)) * tick_size_fp_;
    }
};

// ── Полезная нагрузка для книги ─────────────────────────────
// ЕДИНСТВЕННОЕ, что book видит из внешнего мира.
struct LevelUpdate {
    Ticks px;
    Qty   qty;      // абсолютное значение; Qty{0} = удалить уровень
};
static_assert(std::is_trivially_copyable_v<LevelUpdate>);
static_assert(sizeof(LevelUpdate) == 16);

// ── Frame ───────────────────────────────────────────────────
// Лежит в common (вариант B из разбора): байты + метка времени,
// ничего Binance-специфичного. transport производит, protocol читает.
enum class StreamKind : uint8_t { Depth = 0, Snapshot = 1, Trade = 2, Unknown = 255 };

struct Frame {
    std::span<const std::byte> payload;   // view в буфер источника!
    int64_t    rx_timestamp_ns;           // захвачен НА ГРАНИЦЕ транспорта
    StreamKind kind;
};

} // namespace l2
```

**На что смотреть:**
- `raw()` — один хелпер вместо `static_cast` по всему коду. Цена сильных типов падает почти до нуля.
- Операторы определены **только те, что осмыслены**. `Ticks + Ticks` не существует — сложение двух цен бессмысленно. `next_tick` именован, потому что «+1 тик» — доменная операция.

> **❓ В-1.** Почему `Ticks : int64_t` (знаковый), а `Qty : uint64_t` (беззнаковый)? Подсказка: посмотрите на `ArrayStorage::idx()` ниже и на то, что бывает при вычитании беззнаковых.

> **❓ В-2.** `Frame::payload` — это span. Сформулируйте контракт времени жизни `Frame` (по аналогии с `DepthEvent`) и найдите место в конвейере, где этот контракт мог бы быть нарушен, если бы recorder работал синхронно в IO-потоке.

---

## `common/include/l2/common/result.h` — 🔧 ПЕРИФЕРИЯ

```cpp
#pragma once
#include <variant>
#include <utility>

namespace l2 {

// Минимальный Result для -fno-exceptions зон.
// В C++23 замените на std::expected — интерфейс совместим сознательно.
template<typename T, typename E>
class [[nodiscard]] Result {
    std::variant<T, E> v_;
    explicit Result(std::variant<T, E>&& v) : v_(std::move(v)) {}
public:
    static Result ok(T val)  { return Result{std::variant<T, E>{std::in_place_index<0>, std::move(val)}}; }
    static Result err(E e)   { return Result{std::variant<T, E>{std::in_place_index<1>, std::move(e)}}; }

    bool has_value() const noexcept { return v_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T&       value()       noexcept { return *std::get_if<0>(&v_); }
    const T& value() const noexcept { return *std::get_if<0>(&v_); }
    E&       error()       noexcept { return *std::get_if<1>(&v_); }
    const E& error() const noexcept { return *std::get_if<1>(&v_); }
};

} // namespace l2
```

**Замечание:** `get_if` вместо `get` — осознанно: `get` бросает при неверном индексе, а мы контролируем доступ через `has_value()`. Нарушение контракта (взяли `value()` у ошибки) — UB, как у `optional::operator*`. Это дисциплина «контракт, а не проверка» из дня 3.

---

## `common/include/l2/common/clock.h` — 🔧 ПЕРИФЕРИЯ

```cpp
#pragma once
#include <cstdint>
#include <ctime>

namespace l2 {

// Два источника времени с ОДНИМ интерфейсом — шаблонный параметр
// конвейера, не виртуальный: время зовётся часто.
struct SteadyClock {
    static int64_t now_ns() noexcept {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }
};

// Replay: время = rx_timestamp текущего кадра. Устанавливает конвейер.
class VirtualClock {
    int64_t now_ = 0;
public:
    void set(int64_t ns) noexcept { now_ = ns; }
    int64_t now_ns() const noexcept { return now_; }
};

} // namespace l2
```

> **❓ В-3.** `CLOCK_MONOTONIC_RAW`, а не `CLOCK_REALTIME` и не `CLOCK_MONOTONIC`. Почему для метки `rx_timestamp_ns` не подходит REALTIME? А чем RAW лучше обычного MONOTONIC для *измерений*? (Это заготовка под Проект 2.)

---

# Модуль `book/` — состояние рынка

## `book/include/l2/book/top_of_book.h` — ✍️ ЯДРО

```cpp
#pragma once
#include <l2/common/types.h>

namespace l2 {

struct alignas(64) TopOfBook {
    Ticks    bid_px  = kInvalidTick;
    Qty      bid_qty = Qty{0};
    Ticks    ask_px  = kInvalidTick;
    Qty      ask_qty = Qty{0};
    uint64_t seq     = 0;          // растёт на каждый apply
    uint64_t _pad[3] = {};
};
static_assert(sizeof(TopOfBook) == 64);
static_assert(alignof(TopOfBook) == 64);

constexpr bool has_both_sides(const TopOfBook& t) noexcept {
    return !(t.bid_px == kInvalidTick) && !(t.ask_px == kInvalidTick);
}

} // namespace l2
```

Одна кэш-линия, сигналы читают только это. `seq` — дешёвая проверка «изменилось ли» и защита согласованности при передаче копий.

---

## `book/include/l2/book/storage_map.h` — ✍️ ЯДРО (эталон)

```cpp
#pragma once
#include <l2/common/types.h>
#include <map>
#include <optional>
#include <functional>

namespace l2 {

struct BestLevel { Ticks px; Qty qty; };

// Эталон: медленно, очевидно, корректно. С ним сверяются Array и Hash
// через hash-эквивалентность ДО КОНЦА ПРОЕКТА. Не удалять, не "улучшать".
template<Side S>
class MapStorage {
    // Для бидов лучший = максимальный => greater; begin() всегда лучший.
    using Cmp = std::conditional_t<S == Side::Buy,
                                   std::greater<Ticks>, std::less<Ticks>>;
    std::map<Ticks, Qty, Cmp> levels_;

public:
    void update(Ticks px, Qty q) noexcept {
        if (q == Qty{0}) levels_.erase(px);   // ноль = удаление, не хранение
        else             levels_[px] = q;     // абсолютное, не +=
    }

    std::optional<BestLevel> best() const noexcept {
        if (levels_.empty()) return std::nullopt;
        auto it = levels_.begin();
        return BestLevel{it->first, it->second};
    }

    size_t size() const noexcept { return levels_.size(); }
    void   clear() noexcept { levels_.clear(); }

    // top-N для hash() и crosscheck: out получает до n лучших уровней
    template<typename OutIt>
    size_t top_n(size_t n, OutIt out) const {
        size_t k = 0;
        for (auto it = levels_.begin(); it != levels_.end() && k < n; ++it, ++k)
            *out++ = BestLevel{it->first, it->second};
        return k;
    }

    // Для инвариантов: перебор "в лоб", сверяется с кэшами других storage
    std::optional<BestLevel> scan_best_bruteforce() const noexcept { return best(); }
};

} // namespace l2
```

`std::conditional_t` для компаратора — приём из дня 4: ни одного `if (side == Buy)` в теле, обе инстанциации без ветвлений по стороне.

---

## `book/include/l2/book/storage_array.h` — ✍️ ЯДРО, с пропуском

```cpp
#pragma once
#include <l2/common/types.h>
#include <array>
#include <optional>

namespace l2 {

// Плоский массив: idx = tick & mask — позиция зависит ТОЛЬКО от тика,
// не от базы. "Полоса" [lo_, lo_+kBand) — диапазон валидности;
// recenter двигает границы и зачищает освободившиеся слоты,
// НЕ перемещая ни одного живого значения. В этом весь фокус схемы.
template<Side S>
class ArrayStorage {
    static constexpr size_t kBand = 8192;            // степень двойки!
    static constexpr size_t kMask = kBand - 1;
    static_assert((kBand & kMask) == 0);

    alignas(64) std::array<Qty, kBand> qty_{};       // слоты по (tick & mask)
    int64_t lo_    = 0;                              // нижняя граница полосы (тик)
    Ticks   best_  = kInvalidTick;
    size_t  live_  = 0;

    static size_t slot(Ticks t) noexcept {
        return static_cast<size_t>(static_cast<uint64_t>(raw(t))) & kMask;
    }
    bool in_band(Ticks t) const noexcept {
        const int64_t v = raw(t);
        return v >= lo_ && v < lo_ + int64_t(kBand);
    }
    static bool better(Ticks a, Ticks b) noexcept {   // a лучше b?
        if (b == kInvalidTick) return true;
        if constexpr (S == Side::Buy) return raw(a) > raw(b);
        else                          return raw(a) < raw(b);
    }

    // Скан от best в сторону ХУДШИХ цен до первого живого слота.
    void rescan_best() noexcept {
        if (live_ == 0) { best_ = kInvalidTick; return; }
        int64_t t = raw(best_);
        const int64_t step = (S == Side::Buy) ? -1 : +1;
        for (;;) {
            t += step;
            // выход за полосу при live_>0 невозможен — инвариант
            if (!(qty_[slot(Ticks{t})] == Qty{0})) { best_ = Ticks{t}; return; }
        }
    }

public:
    void update(Ticks px, Qty q) noexcept {
        if (!in_band(px)) {
            if (q == Qty{0}) return;      // удаление вне полосы = no-op (уровень нам неизвестен)
            recenter(px);
        }
        Qty& cell = qty_[slot(px)];
        const bool was = !(cell == Qty{0});
        const bool now = !(q == Qty{0});
        cell = q;
        live_ += size_t(now) - size_t(was);

        if (now && better(px, best_))            best_ = px;
        else if (!now && px == best_)            rescan_best();
    }

    std::optional<BestLevel> best() const noexcept {
        if (best_ == kInvalidTick) return std::nullopt;
        return BestLevel{best_, qty_[slot(best_)]};
    }

    size_t size() const noexcept { return live_; }
    void   clear() noexcept { qty_.fill(Qty{0}); live_ = 0; best_ = kInvalidTick; }

    template<typename OutIt>
    size_t top_n(size_t n, OutIt out) const {
        if (best_ == kInvalidTick) return 0;
        size_t k = 0;
        const int64_t step = (S == Side::Buy) ? -1 : +1;
        for (int64_t t = raw(best_); in_band(Ticks{t}) && k < n; t += step) {
            const Qty q = qty_[slot(Ticks{t})];
            if (!(q == Qty{0})) { *out++ = BestLevel{Ticks{t}, q}; ++k; }
        }
        return k;
    }

    std::optional<BestLevel> scan_best_bruteforce() const noexcept {
        std::optional<BestLevel> r;
        for (int64_t t = lo_; t < lo_ + int64_t(kBand); ++t) {
            const Qty q = qty_[slot(Ticks{t})];
            if (!(q == Qty{0}) && (!r || better(Ticks{t}, r->px)))
                r = BestLevel{Ticks{t}, q};
        }
        return r;
    }

private:
    // ═══════════════════════════════════════════════════════════
    // ✍️ TODO(вы): recenter — пишете сами. Спецификация:
    //
    //   Вход: px вне текущей полосы.
    //   Постусловие 1: px попадает в новую полосу, желательно близко
    //                  к её середине: new_lo ≈ raw(px) - kBand/2.
    //   Постусловие 2: каждый тик, ПОКИНУВШИЙ полосу
    //                  (был в [lo_, new_lo) или [new_lo+kBand, lo_+kBand)),
    //                  имеет qty_[slot] == 0, а live_ уменьшен на
    //                  число живых среди них.
    //   Постусловие 3: ни один слот тика, ОСТАВШЕГОСЯ в полосе,
    //                  не изменён (это свойство схемы tick & mask —
    //                  проверьте, что понимаете, почему оно бесплатно).
    //   Постусловие 4: если best_ покинул полосу — best_ пересчитан
    //                  (или kInvalidTick, если живых не осталось).
    //
    //   Подсказки:
    //   - зачистка = цикл по ВЫБЫВШЕМУ диапазону тиков, не по всему массиву;
    //     размер выбывшего диапазона <= kBand, чаще много меньше;
    //   - если |сдвиг| >= kBand, выбывает вся старая полоса — это
    //     отдельная быстрая ветка: clear() + установка lo_;
    //   - направление сдвига бывает обоим: цена ушла вверх И вниз.
    //
    //   Тесты, которые обязаны быть (напишите ДО реализации):
    //   1. сдвиг вверх на полполосы: живые в пересечении уцелели;
    //   2. сдвиг вниз;
    //   3. сдвиг больше полосы: всё обнулено, live_ == 0;
    //   4. best_ в пересечении → не изменился;
    //   5. best_ выбыл → пересчитан по оставшимся;
    //   6. hash-эквивалентность с MapStorage на потоке со скачком цены.
    // ═══════════════════════════════════════════════════════════
    void recenter(Ticks px) noexcept;
};

} // namespace l2
```

> **❓ В-4.** В `update` удаление вне полосы — no-op с комментарием «уровень нам неизвестен». Свяжите это с обрезанием глубины снимка (5000/1000 уровней) и объясните, почему это *честно*, а не «замели под ковёр». Что обязан сказать об этом README?

> **❓ В-5.** `rescan_best` идёт в сторону худших цен и «не может выйти за полосу при live_ > 0». Докажите этот инвариант. Через какой другой инвариант он гарантируется?

---

## `book/include/l2/book/storage_hash.h` — 🔧 ПЕРИФЕРИЯ (пока)

```cpp
#pragma once
#include <l2/common/types.h>
#include <ankerl/unordered_dense.h>   // добавьте в Dependencies.cmake (FetchContent)
#include <optional>

namespace l2 {

// Open addressing: элементы в непрерывном хранилище, нет узлов.
// Роль в проекте: (1) участник бенчмарка, (2) прототип L3-структуры
// матчинга (там значением станет Level* из пула).
template<Side S>
class HashStorage {
    ankerl::unordered_dense::map<int64_t, Qty> levels_;   // ключ = raw(Ticks)
    Ticks best_ = kInvalidTick;

    static bool better(Ticks a, Ticks b) noexcept {
        if (b == kInvalidTick) return true;
        if constexpr (S == Side::Buy) return raw(a) > raw(b);
        else                          return raw(a) < raw(b);
    }
    void rescan_best() noexcept {                 // O(n) — редкий путь
        best_ = kInvalidTick;
        for (auto& [t, q] : levels_)
            if (better(Ticks{t}, best_)) best_ = Ticks{t};
    }

public:
    void update(Ticks px, Qty q) noexcept {
        if (q == Qty{0}) {
            if (levels_.erase(raw(px)) && px == best_) rescan_best();
        } else {
            levels_[raw(px)] = q;
            if (better(px, best_)) best_ = px;
        }
    }
    std::optional<BestLevel> best() const noexcept {
        if (best_ == kInvalidTick) return std::nullopt;
        return BestLevel{best_, levels_.at(raw(best_))};
    }
    size_t size() const noexcept { return levels_.size(); }
    void   clear() noexcept { levels_.clear(); best_ = kInvalidTick; }

    template<typename OutIt>
    size_t top_n(size_t n, OutIt out) const;      // соберите n лучших: частичная сортировка
    std::optional<BestLevel> scan_best_bruteforce() const noexcept;
};

} // namespace l2
```

> **❓ В-6.** Ключ — `int64_t`, а не `Ticks`. Это вынужденная уступка (хеш-функция для enum class требует специализации). Назовите два способа сделать ключом настоящий `Ticks` и цену каждого. Стоит ли оно того здесь?

> **❓ В-7.** `rescan_best` здесь O(n) по всем уровням — против O(шагов до соседа) у массива. Почему для реального стакана это почти всегда приемлемо, и какой рыночный сценарий делает это больно? (Подсказка: что происходит с лучшими уровнями при резком движении цены.)

---

## `book/include/l2/book/book.h` — ✍️ ЯДРО

```cpp
#pragma once
#include <l2/common/types.h>
#include <l2/book/top_of_book.h>
#include <span>
#include <vector>
#include <cstdint>

namespace l2 {

template<typename BidS, typename AskS>
class BookL2 {
    BidS      bids_;
    AskS      asks_;
    TopOfBook top_;

public:
    // Снимок: полная замена состояния.
    void apply_snapshot(std::span<const LevelUpdate> bids,
                        std::span<const LevelUpdate> asks) noexcept {
        bids_.clear(); asks_.clear();
        for (auto& u : bids) bids_.update(u.px, u.qty);
        for (auto& u : asks) asks_.update(u.px, u.qty);
        refresh_top();
        check_invariants();
    }

    // Дельта. Вход УЖЕ валидирован секвенсором — книга доверяет.
    void apply(std::span<const LevelUpdate> bids,
               std::span<const LevelUpdate> asks) noexcept {
        for (auto& u : bids) bids_.update(u.px, u.qty);
        for (auto& u : asks) asks_.update(u.px, u.qty);
        refres_top_and_bump();
        check_invariants();
    }

    const TopOfBook& top() const noexcept { return top_; }

    uint64_t hash() const noexcept;                       // book.cpp / book.inl
    size_t   to_levels(Side s, size_t n,
                       std::span<BestLevel> out) const;   // для crosscheck

    void clear() noexcept { bids_.clear(); asks_.clear(); top_ = {}; }

private:
    void refresh_top() noexcept {
        const auto b = bids_.best();
        const auto a = asks_.best();
        top_.bid_px  = b ? b->px  : kInvalidTick;
        top_.bid_qty = b ? b->qty : Qty{0};
        top_.ask_px  = a ? a->px  : kInvalidTick;
        top_.ask_qty = a ? a->qty : Qty{0};
    }
    void refresh_top_and_bump() noexcept { refresh_top(); ++top_.seq; }

    void check_invariants() const noexcept;               // invariants.h
};

// ── Явная инстанциация: набор закрыт, объявляем extern ──────
// Без extern каждая TU, включившая book.h, инстанцирует всё заново —
// и смысл book.cpp исчезает.
template<Side> class MapStorage; template<Side> class ArrayStorage; template<Side> class HashStorage;

using BookMap   = BookL2<MapStorage<Side::Buy>,   MapStorage<Side::Sell>>;
using BookArray = BookL2<ArrayStorage<Side::Buy>, ArrayStorage<Side::Sell>>;
using BookHash  = BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;

extern template class BookL2<MapStorage<Side::Buy>,   MapStorage<Side::Sell>>;
extern template class BookL2<ArrayStorage<Side::Buy>, ArrayStorage<Side::Sell>>;
extern template class BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;

} // namespace l2
```

Опечатка `refres_top_and_bump` в вызове оставлена **намеренно** — это ваш первый компилятор-тест на внимательность при переписывании руками.

> **❓ В-8.** `extern template` в заголовке + `template class` в book.cpp — восстановите механику: что произойдёт при (а) наличии только первого, (б) только второго, (в) обоих? Какая ошибка в каком случае и на какой стадии (компиляция/линковка)?

> **❓ В-9.** `apply_snapshot` не делает `++top_.seq` (нет bump). Это баг или решение? Аргументируйте через потребителей `seq` — сигналы и crosscheck. (Здесь есть за что зацепиться в обе стороны — выберите и защитите.)

---

## `book/include/l2/book/invariants.h` — ✍️ ЯДРО

```cpp
#pragma once
#include <cassert>

// Включается флагом L2_CHECK_INVARIANTS (см. l2::options),
// НЕ привязан к Debug: режим "Release + проверки" — наш основной
// режим replay-валидации.

namespace l2 {

template<typename BidS, typename AskS>
void BookL2<BidS, AskS>::check_invariants() const noexcept {
#if defined(L2_CHECK_INVARIANTS)
    // 1. Не скрещен. На дифф-потоке скрещивание = наша порча данных.
    if (has_both_sides(top_))
        assert(raw(top_.bid_px) < raw(top_.ask_px) && "crossed book");

    // 2. Кэш вершины не врёт (ловит рассинхрон best_ у Array/Hash).
    {
        const auto bb = bids_.scan_best_bruteforce();
        const auto cb = bids_.best();
        assert(bb.has_value() == cb.has_value());
        if (bb) assert(bb->px == cb->px && bb->qty == cb->qty);
        const auto ba = asks_.scan_best_bruteforce();
        const auto ca = asks_.best();
        assert(ba.has_value() == ca.has_value());
        if (ba) assert(ba->px == ca->px && ba->qty == ca->qty);
    }
#endif
}

} // namespace l2
```

Инварианты «нет резидентных нулей» и «live_ соответствует» живут внутри storage-тестов, а не здесь: снаружи книги их не видно без нарушения инкапсуляции. Разделение то же, что модульное: каждый слой проверяет своё.

---

## `book/src/book.cpp` — 🔧 ПЕРИФЕРИЯ

```cpp
#include <l2/book/book.h>
#include <l2/book/storage_map.h>
#include <l2/book/storage_array.h>
#include <l2/book/storage_hash.h>
#include <l2/book/invariants.h>
#include <xxhash.h>

namespace l2 {

template<typename BidS, typename AskS>
uint64_t BookL2<BidS, AskS>::hash() const noexcept {
    constexpr size_t kHashDepth = 32;      // меньше полосы Array — иначе
                                            // ложные расхождения Map vs Array
    BestLevel buf[kHashDepth];
    XXH64_state_t st;
    XXH64_reset(&st, 0);
    size_t n = bids_.top_n(kHashDepth, buf);
    XXH64_update(&st, buf, n * sizeof(BestLevel));
    n = asks_.top_n(kHashDepth, buf);
    XXH64_update(&st, buf, n * sizeof(BestLevel));
    return XXH64_digest(&st);
}

template<typename BidS, typename AskS>
size_t BookL2<BidS, AskS>::to_levels(Side s, size_t n, std::span<BestLevel> out) const {
    return (s == Side::Buy) ? bids_.top_n(n, out.begin())
                            : asks_.top_n(n, out.begin());
}

template class BookL2<MapStorage<Side::Buy>,   MapStorage<Side::Sell>>;
template class BookL2<ArrayStorage<Side::Buy>, ArrayStorage<Side::Sell>>;
template class BookL2<HashStorage<Side::Buy>,  HashStorage<Side::Sell>>;

} // namespace l2
```

> **❓ В-10.** `XXH64_update(&st, buf, n * sizeof(BestLevel))` хеширует структуру побайтово. `BestLevel` — это `{Ticks px; Qty qty}` = 16 байт без паддинга. А теперь представьте, что кто-то добавил в `BestLevel` поле `uint8_t flags`. Что тихо сломается в хешировании и почему детерминизм-тест это поймает, а может и НЕ поймать? Каким static_assert защититься заранее?

---

# Модуль `protocol/`

## `protocol/include/l2/protocol/events.h` — уже разобран, финальная версия

```cpp
#pragma once
#include <l2/common/types.h>
#include <span>

namespace l2 {

struct DepthEvent {
    uint64_t U;
    uint64_t u;
    uint64_t pu;                                   // futures; на споте = kNoPu
    static constexpr uint64_t kNoPu = ~0ull;

    int64_t  event_time_us;
    int64_t  rx_timestamp_ns;
    SymbolId symbol;

    std::span<const LevelUpdate> bids;             // view в буферы парсера!
    std::span<const LevelUpdate> asks;             // валидно до следующего parse()
};

struct SnapshotEvent {
    uint64_t last_update_id;
    int64_t  rx_timestamp_ns;
    SymbolId symbol;
    std::span<const LevelUpdate> bids;
    std::span<const LevelUpdate> asks;
};

enum class ParseError : uint8_t {
    BadJson, UnknownStream, TooManyLevels, BadDecimal, PriceOffTick
};

} // namespace l2
```

## `protocol/include/l2/protocol/venue.h` — ✍️ ЯДРО (вы уже писали в день 6)

```cpp
#pragma once
#include <l2/protocol/events.h>

namespace l2 {

// ⚠ Сверьте правила с ТЕКУЩЕЙ документацией Binance перед реализацией:
// spot: binance-spot-api-docs, "How to manage a local order book correctly"
// futures: тот же раздел futures-доков. Правила НАМЕРЕННО разные.

struct SpotPolicy {
    static bool is_stale(const DepthEvent& e, uint64_t last_update_id) noexcept {
        return e.u <= last_update_id;
    }
    static bool is_first_applicable(const DepthEvent& e, uint64_t last_update_id) noexcept {
        return e.U <= last_update_id + 1 && last_update_id + 1 <= e.u;
    }
    static bool is_contiguous(const DepthEvent& e, uint64_t prev_u) noexcept {
        return e.U == prev_u + 1;
    }
};

struct FuturesPolicy {
    static bool is_stale(const DepthEvent& e, uint64_t last_update_id) noexcept {
        return e.u < last_update_id;
    }
    static bool is_first_applicable(const DepthEvent& e, uint64_t last_update_id) noexcept {
        return e.U <= last_update_id && last_update_id <= e.u;
    }
    static bool is_contiguous(const DepthEvent& e, uint64_t prev_u) noexcept {
        return e.pu == prev_u;                    // ← ВСЁ различие протоколов
    }
};

} // namespace l2
```

## `protocol/include/l2/protocol/event_buffer.h`

Написан полностью в прошлом сообщении — перенесите как есть. Не дублирую.

## `protocol/include/l2/protocol/sequencer.h` — ✍️ ЯДРО

```cpp
#pragma once
#include <l2/protocol/events.h>
#include <l2/protocol/event_buffer.h>

namespace l2 {

enum class SeqState  : uint8_t { Disconnected, Buffering, Synced, Desynced };
enum class SeqAction : uint8_t { Apply, Drop, RequestSnapshot, Resync };

struct SeqStats {
    uint64_t gaps = 0, dropped = 0, resyncs = 0, buffered = 0, buffer_overflows = 0;
};

template<typename Policy>
class Sequencer {
    SeqState    state_ = SeqState::Disconnected;
    uint64_t    prev_u_ = 0;
    EventBuffer buffer_;
    SeqStats    stats_;

public:
    SeqAction on_connected() noexcept {
        state_ = SeqState::Buffering;
        buffer_.clear();
        return SeqAction::RequestSnapshot;
    }

    SeqAction on_event(const DepthEvent& e) noexcept {
        switch (state_) {
        case SeqState::Synced:
            if (Policy::is_contiguous(e, prev_u_)) {     // инлайн: 1 сравнение
                prev_u_ = e.u;
                return SeqAction::Apply;
            }
            ++stats_.gaps;
            state_ = SeqState::Desynced;
            return SeqAction::Resync;

        case SeqState::Buffering:
            if (buffer_.push(e)) { ++stats_.buffered; return SeqAction::Drop; }
            ++stats_.buffer_overflows;                    // снимок завис
            state_ = SeqState::Desynced;
            return SeqAction::Resync;

        case SeqState::Disconnected:
        case SeqState::Desynced:
            ++stats_.dropped;
            return SeqAction::Drop;
        }
        return SeqAction::Drop;                           // unreachable
    }

    // Сшивка. Sink — шаблонный callback: emit(const DepthEvent&) для каждого
    // применимого буферизованного события. Порядок и валидация — здесь,
    // применение к книге — у конвейера. Компоненты соседей не знают.
    template<typename Sink>
    SeqAction on_snapshot(const SnapshotEvent& s, Sink&& emit) noexcept {
        if (state_ != SeqState::Buffering) { return SeqAction::Drop; }

        size_t i = 0;
        // 1. отбросить устаревшие
        while (i < buffer_.size() && Policy::is_stale(buffer_.view(i), s.last_update_id))
            ++i;

        if (i < buffer_.size()) {
            // 2. первое неустаревшее обязано накрывать снимок
            if (!Policy::is_first_applicable(buffer_.view(i), s.last_update_id)) {
                buffer_.clear();
                state_ = SeqState::Desynced;
                ++stats_.resyncs;
                return SeqAction::Resync;                 // дыра снимок↔буфер
            }
            // 3. применить хвост с обычной проверкой непрерывности
            prev_u_ = buffer_.view(i).u;
            emit(buffer_.view(i));
            for (++i; i < buffer_.size(); ++i) {
                const DepthEvent ev = buffer_.view(i);
                if (!Policy::is_contiguous(ev, prev_u_)) {
                    buffer_.clear();
                    state_ = SeqState::Desynced;
                    ++stats_.gaps; ++stats_.resyncs;
                    return SeqAction::Resync;             // дыра ВНУТРИ буфера
                }
                prev_u_ = ev.u;
                emit(ev);
            }
        } else {
            // буфер целиком устарел или пуст: якорь = снимок,
            // непрерывность живых проверит is_first_applicable? НЕТ —
            // см. В-12.
            prev_u_ = s.last_update_id;
        }

        buffer_.clear();
        state_ = SeqState::Synced;
        return SeqAction::Apply;      // сигнал конвейеру: снимок применить к книге
    }

    SeqAction on_disconnect() noexcept {
        state_ = SeqState::Disconnected;
        buffer_.clear();
        return SeqAction::Drop;
    }

    SeqAction on_book_invalid() noexcept {                // книга сообщила о скрещивании
        state_ = SeqState::Desynced;
        ++stats_.resyncs;
        return SeqAction::Resync;
    }

    SeqState state() const noexcept { return state_; }
    const SeqStats& stats() const noexcept { return stats_; }
};

} // namespace l2
```

> **❓ В-11.** Sink — шаблонный параметр метода, не `std::function`. Три причины, почему здесь это правильно, — и одна ситуация, в которой `std::function` стал бы оправдан. (Прямая связь с критикой `DispatchCallback` в leapfirst.)

> **❓ В-12.** Ветка «буфер целиком устарел»: я поставил `prev_u_ = s.last_update_id` и вопросительный комментарий. Здесь закопана настоящая тонкость: следующее ЖИВОЕ событие будет проверено через `is_contiguous(e, prev_u_)`. Для спота `U == last_update_id + 1` — совпадает ли это с документированным правилом первого события (`U <= last+1 <= u`)? Найдите сценарий, где строгая проверка ложно сработает, и предложите исправление состояния/логики. Это самый ценный вопрос документа — он про то, как выглядит реальный баг сшивки.

> **❓ В-13.** `on_snapshot` возвращает `Apply` в значении «снимок применить к книге», а буферный хвост уехал через Sink. Оцените этот интерфейс критически: какие два разных смысла смешаны в одном enum? Предложите альтернативу (новый Action? отдельный тип результата?) и её цену.

---

## `protocol/include/l2/protocol/parser.h` — 🔧 ПЕРИФЕРИЯ

```cpp
#pragma once
#include <l2/common/types.h>
#include <l2/common/result.h>
#include <l2/protocol/events.h>
#include <array>
#include <memory>
#include <variant>

namespace l2 {

using ParsedEvent = std::variant<DepthEvent, SnapshotEvent>;

class Parser {
public:
    explicit Parser(TickConverter conv);
    ~Parser();
    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;

    // Возвращённый event — view в буферы парсера. Валиден до следующего parse().
    Result<ParsedEvent, ParseError> parse(const Frame& f) noexcept;

private:
    struct Impl;                          // simdjson спрятан за pimpl:
    std::unique_ptr<Impl> impl_;          // потребители не видят его заголовков
};

} // namespace l2
```

> **❓ В-14.** Раз есть `~Parser()` объявленный (нужен для pimpl с incomplete type) — вспомните вопрос 3 экзамена. Что я обязан был сделать в этом заголовке, чтобы Parser остался перемещаемым, и что случится, если строчки `Parser(Parser&&)` убрать?

## `protocol/src/parser.cpp` — 🔧 ПЕРИФЕРИЯ (AI-зона, но разберите контракт simdjson)

```cpp
#include <l2/protocol/parser.h>
#include <simdjson.h>
#include <cstring>

namespace l2 {

namespace {
// decimal-строка "64999.10" → fp*1e8. Ваш parse_fp8 из leapfirst —
// перенесите его; здесь компактная версия для полноты документа.
bool parse_fp8(std::string_view s, uint64_t& out) noexcept {
    uint64_t ip = 0, fp = 0; int fdig = 0; size_t i = 0;
    if (s.empty()) return false;
    for (; i < s.size() && s[i] != '.'; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        ip = ip * 10 + uint64_t(s[i] - '0');
    }
    if (i < s.size()) {                    // дробная часть
        for (++i; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            if (fdig < 8) { fp = fp * 10 + uint64_t(s[i] - '0'); ++fdig; }
        }
    }
    while (fdig++ < 8) fp *= 10;
    out = ip * kFpScale + fp;
    return true;
}
} // anonymous namespace

struct Parser::Impl {
    simdjson::ondemand::parser sj;
    simdjson::padded_string    padded;             // simdjson требует padding!
    TickConverter conv;
    std::array<LevelUpdate, 1024> bid_buf, ask_buf;

    explicit Impl(TickConverter c) : conv(c) {}

    Result<ParsedEvent, ParseError> parse(const Frame& f) noexcept {
        // simdjson требует SIMDJSON_PADDING байт за концом ввода —
        // копируем в padded_string. Это единственная копия на пути.
        padded = simdjson::padded_string(
            reinterpret_cast<const char*>(f.payload.data()), f.payload.size());

        simdjson::ondemand::document doc;
        if (sj.iterate(padded).get(doc)) return err(ParseError::BadJson);

        // combined stream: {"stream": "...", "data": {...}}
        simdjson::ondemand::object data;
        if (doc["data"].get(data)) return err(ParseError::UnknownStream);

        if (f.kind == StreamKind::Depth)    return parse_depth(data, f);
        // Snapshot приходит из REST тем же Frame-каналом
        if (f.kind == StreamKind::Snapshot) return parse_snapshot(data, f);
        return err(ParseError::UnknownStream);
    }

    Result<ParsedEvent, ParseError> parse_depth(simdjson::ondemand::object& d,
                                                const Frame& f) noexcept {
        DepthEvent e{};
        e.rx_timestamp_ns = f.rx_timestamp_ns;
        e.pu = DepthEvent::kNoPu;

        uint64_t tmp;
        if (d["U"].get(e.U) || d["u"].get(e.u)) return err(ParseError::BadJson);
        if (!d["pu"].get(tmp)) e.pu = tmp;                 // есть только на futures
        int64_t E;
        if (d["E"].get(E)) return err(ParseError::BadJson);
        e.event_time_us = E * 1000;                        // ms → us (без timeUnit)

        size_t nb = 0, na = 0;
        if (fill_levels(d, "b", bid_buf, nb) != ParseError{} ||
            fill_levels(d, "a", ask_buf, na) != ParseError{})
            return err(ParseError::BadDecimal);

        e.bids = {bid_buf.data(), nb};
        e.asks = {ask_buf.data(), na};
        return Result<ParsedEvent, ParseError>::ok(e);
    }

    ParseError fill_levels(simdjson::ondemand::object& d, const char* key,
                           std::array<LevelUpdate, 1024>& buf, size_t& n) noexcept {
        n = 0;
        simdjson::ondemand::array arr;
        if (d[key].get(arr)) return ParseError::BadJson;
        for (auto lvl : arr) {
            if (n == buf.size()) return ParseError::TooManyLevels;
            simdjson::ondemand::array pair;
            if (lvl.get(pair)) return ParseError::BadJson;
            auto it = pair.begin();
            std::string_view ps, qs;
            if ((*it).get(ps)) return ParseError::BadJson;
            ++it;
            if ((*it).get(qs)) return ParseError::BadJson;
            uint64_t pfp, qfp;
            if (!parse_fp8(ps, pfp) || !parse_fp8(qs, qfp))
                return ParseError::BadDecimal;
            if (!conv.is_valid_price(pfp)) return ParseError::PriceOffTick;
            buf[n++] = LevelUpdate{conv.to_ticks(pfp), Qty{qfp}};
        }
        return ParseError{};
    }

    // parse_snapshot: аналогично, поля lastUpdateId/bids/asks — допишите
    // по образцу parse_depth. 🔧 Делегируемо.
    Result<ParsedEvent, ParseError> parse_snapshot(simdjson::ondemand::object&,
                                                   const Frame&) noexcept;

    static Result<ParsedEvent, ParseError> err(ParseError e) noexcept {
        return Result<ParsedEvent, ParseError>::err(e);
    }
};

Parser::Parser(TickConverter c) : impl_(std::make_unique<Impl>(c)) {}
Parser::~Parser() = default;
Parser::Parser(Parser&&) noexcept = default;
Parser& Parser::operator=(Parser&&) noexcept = default;

Result<ParsedEvent, ParseError> Parser::parse(const Frame& f) noexcept {
    return impl_->parse(f);
}

} // namespace l2
```

**Что здесь стоит вашего внимания несмотря на 🔧-метку:**
- `padded_string` — копия. simdjson ondemand требует padding за концом буфера; лучший вариант — чтобы **транспорт** резервировал padding в своих буферах чтения, тогда копия исчезает. Оставлено как upgrade: сначала корректно, потом быстро, с бенчмарком до/после.
- `symbol` не заполняется — резолв `stream`-имени в `SymbolId` через таблицу из exchangeInfo допишите при мультисимвольности.
- `timeUnit=MICROSECOND` в URL подключения меняет масштаб `E` — согласуйте с `* 1000`.

---

# Модуль `transport/` — 🔧 ПЕРИФЕРИЯ целиком

## `transport/include/l2/transport/frame_source.h`

```cpp
#pragma once
#include <l2/common/types.h>

namespace l2 {

// Единственный виртуальный интерфейс проекта. Холодная граница:
// один вызов на сетевой кадр.
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    // false = источник исчерпан (EOF файла / фатальный обрыв).
    // Возвращённый Frame валиден до следующего next().
    virtual bool next(Frame& out) = 0;
};

} // namespace l2
```

## `transport/include/l2/transport/file_source.h` + реализация

```cpp
#pragma once
#include <l2/transport/frame_source.h>
#include <cstddef>

namespace l2 {

// Формат кадра на диске:
// [u32 len][i64 rx_timestamp_ns][u8 kind][payload: len байт]
class FileSource final : public IFrameSource {
    const std::byte* base_ = nullptr;
    size_t size_ = 0, off_ = 0;
    int fd_ = -1;
public:
    explicit FileSource(const char* path);      // mmap, MADV_SEQUENTIAL
    ~FileSource() override;                     // munmap, close
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    bool next(Frame& out) override;
};

} // namespace l2
```

```cpp
// transport/src/file_source.cpp
#include <l2/transport/file_source.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace l2 {

FileSource::FileSource(const char* path) {
    fd_ = ::open(path, O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("open failed");
    struct stat st{};
    ::fstat(fd_, &st);
    size_ = size_t(st.st_size);
    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (p == MAP_FAILED) throw std::runtime_error("mmap failed");
    ::madvise(p, size_, MADV_SEQUENTIAL);       // подсказка prefetcher'у ядра
    base_ = static_cast<const std::byte*>(p);
}

FileSource::~FileSource() {
    if (base_) ::munmap(const_cast<std::byte*>(base_), size_);
    if (fd_ >= 0) ::close(fd_);
}

bool FileSource::next(Frame& out) {
    constexpr size_t kHdr = 4 + 8 + 1;
    if (off_ + kHdr > size_) return false;      // EOF (или обрезанный хвост)
    uint32_t len;  std::memcpy(&len, base_ + off_, 4);
    int64_t  ts;   std::memcpy(&ts,  base_ + off_ + 4, 8);
    uint8_t  kind; std::memcpy(&kind, base_ + off_ + 12, 1);
    if (off_ + kHdr + len > size_) return false;
    out = Frame{{base_ + off_ + kHdr, len}, ts, StreamKind{kind}};
    off_ += kHdr + len;
    return true;
}

} // namespace l2
```

`memcpy` вместо `reinterpret_cast`-чтения — единственный законный способ читать невыровненные данные без UB (strict aliasing, день 3). Компилятор сворачивает его в одну инструкцию загрузки.

## `transport/include/l2/transport/recorder.h`

```cpp
#pragma once
#include <l2/common/types.h>
#include <l2/common/spsc.h>        // ← перенесите ваш SPSCQueue из leapfirst
#include <thread>
#include <atomic>

namespace l2 {

// Журнал сырых кадров. Пишущий поток свой: fsync-спайки диска
// не должны останавливать IO-поток. Переполнение очереди =
// счётчик + потеря кадра ЖУРНАЛА (не конвейера!) — громко в статистику.
class Recorder {
    struct Rec {                                  // материализованная копия
        int64_t ts; uint32_t len; uint8_t kind;
        std::byte payload[4096];                  // ❓ В-15
    };
    SPSCQueue<Rec, 4096> q_;
    std::thread writer_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> dropped_{0};
    int fd_;
public:
    explicit Recorder(const char* path);
    ~Recorder();

    void tap(const Frame& f) noexcept;            // вызывается IO-потоком
    uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
private:
    void writer_loop();                            // батч-запись, O_APPEND
};

} // namespace l2
```

> **❓ В-15.** `payload[4096]` фиксированного размера в слоте очереди — то самое решение, которое мы **отвергли** для EventBuffer («64 МБ из которых полпроцента»). Почему здесь оно приемлемо, а там нет? Посчитайте память и назовите принципиальное отличие профиля данных. А затем — предложите, как убрать и это ограничение (подсказка: чем журнал отличается от очереди произвольных сообщений).

## `transport/include/l2/transport/ws_source.h` — эскиз рабочей формы

```cpp
#pragma once
#include <l2/transport/frame_source.h>
#include <memory>

namespace l2 {

struct WsConfig {
    const char* host;                   // stream.binance.com
    const char* port;                   // "9443"
    const char* target;                 // /stream?streams=...&timeUnit=MICROSECOND
    int64_t     max_conn_age_ns = 23ll * 3600 * 1'000'000'000;   // < 24h лимита
};

// Блокирующий beast-клиент. Для одного соединения на пиннутом IO-потоке
// синхронное чтение проще и не медленнее асинхронного.
// Beast сам отвечает pong на ping в процессе read().
class WsSource final : public IFrameSource {
    struct Impl;                         // beast/ssl за pimpl
    std::unique_ptr<Impl> impl_;
public:
    explicit WsSource(WsConfig cfg);
    ~WsSource() override;

    // Реконнект — ВНУТРИ next(): по ошибке чтения или возрасту
    // соединения переподключается и возвращает синтетический
    // Frame{kind=Disconnect}... нет. См. В-16.
    bool next(Frame& out) override;
};

} // namespace l2
```

> **❓ В-16.** Обрыв соединения должен доехать до секвенсора (`on_disconnect` / `on_connected` → Buffering → RequestSnapshot). Через интерфейс `bool next(Frame&)` это выразить нечем: false означает «источник кончился». Три варианта: (а) расширить StreamKind значениями Connected/Disconnected и гнать их как кадры; (б) сменить сигнатуру next на Result<Frame, SourceEvent>; (в) отдельный колбек on_state. Выберите, аргументируйте против двух других — и проверьте выбор требованием: **replay должен уметь воспроизводить реконнекты из журнала**. Какой вариант единственный проходит эту проверку без спецслучаев?

`rest_client.h` — по образцу leapfirst HTTP + предыдущего разбора (weight-лимиты, backoff с джиттером, ответ → `Frame{kind=Snapshot}` в SPSC конвейера). 🔧 Полностью делегируемо.

---

# Модуль `signals/` — ✍️ ЯДРО (маленькое, но с ловушкой)

## `signals/include/l2/signals/ofi.h`

```cpp
#pragma once
#include <l2/book/top_of_book.h>

namespace l2 {

// Cont–Kukanov–Stoikov (2014), определение e_n по обновлениям вершины.
class OfiCalculator {
    TopOfBook prev_{};
    bool has_prev_ = false;
public:
    // Возвращает e_n (0.0 для первого наблюдения).
    double on_top_update(const TopOfBook& t) noexcept {
        if (!has_prev_ || !has_both_sides(t) || !has_both_sides(prev_)) {
            prev_ = t; has_prev_ = true; return 0.0;
        }
        double e = 0.0;
        // бид: улучшение/удержание цены добавляет текущий объём,
        //      ухудшение/удержание вычитает предыдущий
        if (raw(t.bid_px) >= raw(prev_.bid_px)) e += double(raw(t.bid_qty));
        if (raw(t.bid_px) <= raw(prev_.bid_px)) e -= double(raw(prev_.bid_qty));
        // аск: зеркально со знаком минус
        if (raw(t.ask_px) <= raw(prev_.ask_px)) e -= double(raw(t.ask_qty));
        if (raw(t.ask_px) >= raw(prev_.ask_px)) e += double(raw(prev_.ask_qty));
        prev_ = t;
        return e;
    }
    void reset() noexcept { has_prev_ = false; }
};

} // namespace l2
```

> **❓ В-17.** При равенстве цен (`==`) срабатывают **оба** условия стороны — и плюс, и минус. Раскройте, что даёт их сумма при неизменной цене и изменившемся объёме, и сверьте с формулой из статьи: это совпадение или моя ошибка? (Возьмите статью, раздел с определением e_n, и проверьте по индикаторным функциям. Не верьте документу — верьте первоисточнику.)

> **❓ В-18.** `reset()` — когда конвейер ОБЯЗАН его звать? Свяжите с состояниями секвенсора: какой переход делает `prev_` ложью, и что произойдёт со значением e_n, если reset забыть?

## `signals/include/l2/signals/microprice.h`

```cpp
#pragma once
#include <l2/book/top_of_book.h>

namespace l2 {

// Веса НАМЕРЕННО выглядят перевёрнутыми: тяжёлый бид тянет оценку к АСКУ.
// Написать наоборот = сигнал противоположного знака. Проверка — markout.
inline double weighted_mid(const TopOfBook& t) noexcept {
    const double qb = double(raw(t.bid_qty));
    const double qa = double(raw(t.ask_qty));
    const double I  = qb / (qb + qa);
    return I * double(raw(t.ask_px)) + (1.0 - I) * double(raw(t.bid_px));
}

inline double mid(const TopOfBook& t) noexcept {
    return 0.5 * (double(raw(t.bid_px)) + double(raw(t.ask_px)));
}

} // namespace l2
```

---

# `app/replay_main.cpp` — 🔧, но прочитайте: тут вся архитектура

```cpp
#include <l2/transport/file_source.h>
#include <l2/protocol/parser.h>
#include <l2/protocol/sequencer.h>
#include <l2/protocol/venue.h>
#include <l2/book/book.h>
#include <l2/book/storage_array.h>
#include <l2/signals/ofi.h>
#include <l2/signals/microprice.h>
#include <cstdio>

using namespace l2;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: replay <frames.bin> <tick_fp> <hash.log>\n"); return 1; }

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
                     (unsigned long long)e.u,
                     (unsigned long long)book.hash(),
                     en, weighted_mid(book.top()));
    };

    seq.on_connected();                              // replay стартует как live

    Frame f;
    while (src.next(f)) {
        auto r = parser.parse(f);
        if (!r) continue;                            // счётчик ошибок — допишите

        if (auto* d = std::get_if<DepthEvent>(&r.value())) {
            switch (seq.on_event(*d)) {
            case SeqAction::Apply: apply_depth(*d); break;
            case SeqAction::Resync: /* в replay: ждём Snapshot-кадр из журнала */ break;
            default: break;
            }
        } else if (auto* s = std::get_if<SnapshotEvent>(&r.value())) {
            const auto a = seq.on_snapshot(*s, apply_depth);
            if (a == SeqAction::Apply) {
                book.apply_snapshot(s->bids, s->asks);   // ❓ В-19
                std::fprintf(hlog, "SNAP %llu %016llx\n",
                             (unsigned long long)s->last_update_id,
                             (unsigned long long)book.hash());
            }
        }
    }
    std::fclose(hlog);
    return 0;
}
```

> **❓ В-19.** Здесь баг порядка, и он настоящий. `on_snapshot` вызывает `apply_depth` для буферного хвоста **до** того, как `main` применил сам снимок к книге. Восстановите правильный порядок операций сшивки и предложите исправление — интерфейсное (что должен вернуть/принять `on_snapshot`) или локальное в main. Это второй по ценности вопрос документа: он показывает, как правильная логика в модуле собирается в неправильную систему на композиции.

---

# `tests/` — представительные фрагменты

## `test_sequencer.cpp` — ✍️ шаблон, таблицы допишете

```cpp
#include <gtest/gtest.h>
#include <l2/protocol/sequencer.h>
#include <l2/protocol/venue.h>

using namespace l2;

namespace {
DepthEvent ev(uint64_t U, uint64_t u, uint64_t pu = DepthEvent::kNoPu) {
    DepthEvent e{};
    e.U = U; e.u = u; e.pu = pu;
    return e;                                     // span'ы пустые — секвенсору хватает
}
} // namespace

TEST(SpotSeq, ContiguousApplies) {
    Sequencer<SpotPolicy> s;
    s.on_connected();
    SnapshotEvent snap{}; snap.last_update_id = 100;
    s.on_snapshot(snap, [](const DepthEvent&){});
    EXPECT_EQ(s.on_event(ev(101, 105)), SeqAction::Apply);
    EXPECT_EQ(s.on_event(ev(106, 110)), SeqAction::Apply);
}

TEST(SpotSeq, GapIsFatal) {
    Sequencer<SpotPolicy> s;
    s.on_connected();
    SnapshotEvent snap{}; snap.last_update_id = 100;
    s.on_snapshot(snap, [](const DepthEvent&){});
    EXPECT_EQ(s.on_event(ev(101, 105)), SeqAction::Apply);
    EXPECT_EQ(s.on_event(ev(107, 110)), SeqAction::Resync);   // дыра: 106 пропал
    EXPECT_EQ(s.on_event(ev(111, 115)), SeqAction::Drop);     // Desynced молчит
    EXPECT_EQ(s.stats().gaps, 1u);
}

TEST(FuturesSeq, PuChain) {
    Sequencer<FuturesPolicy> s;
    s.on_connected();
    SnapshotEvent snap{}; snap.last_update_id = 100;
    s.on_snapshot(snap, [](const DepthEvent&){});
    // futures: непрерывность по pu, НЕ по арифметике U
    EXPECT_EQ(s.on_event(ev(95, 105, /*pu=*/100)), SeqAction::Apply);
    EXPECT_EQ(s.on_event(ev(200, 300, /*pu=*/105)), SeqAction::Apply);  // разрыв ID — норм!
    EXPECT_EQ(s.on_event(ev(301, 310, /*pu=*/301)), SeqAction::Resync); // pu != 300
}

// ✍️ Допишите таблицы: устаревшие события в буфере, сшивка с хвостом,
// переполнение буфера, дыра внутри буфера, on_disconnect посреди Synced,
// и — обязательно — сценарий из В-12, когда его решите.
```

## `test_book_typed.cpp` — каркас

```cpp
#include <gtest/gtest.h>
#include <l2/book/book.h>
#include <l2/book/storage_map.h>
#include <l2/book/storage_array.h>
#include <l2/book/storage_hash.h>

using namespace l2;

template<typename B> class BookTest : public ::testing::Test {};
using Books = ::testing::Types<BookMap, BookArray, BookHash>;
TYPED_TEST_SUITE(BookTest, Books);

TYPED_TEST(BookTest, ZeroQtyDeletes) {
    TypeParam b;
    const LevelUpdate up[]  = {{Ticks{100}, Qty{5}}};
    const LevelUpdate del[] = {{Ticks{100}, Qty{0}}};
    b.apply(up, {});
    EXPECT_EQ(b.top().bid_px, Ticks{100});
    b.apply(del, {});
    EXPECT_EQ(b.top().bid_px, kInvalidTick);
}

TYPED_TEST(BookTest, AbsoluteNotDelta) {
    TypeParam b;
    const LevelUpdate a[] = {{Ticks{100}, Qty{5}}};
    const LevelUpdate c[] = {{Ticks{100}, Qty{3}}};
    b.apply(a, {}); b.apply(c, {});
    EXPECT_EQ(b.top().bid_qty, Qty{3});          // 3, не 8!
}

// ✍️ Допишите: NeverCrossed (property на случайном потоке),
// HashEquivalence (тот же поток в TypeParam и в BookMap → equal hash),
// RecenterSurvival (для Array — тесты 1–6 из TODO).
```

---

# Открытые вопросы — сводка

**Из этого документа:** В-1 … В-19. Минимум для продолжения: В-8, В-11, В-12, В-19 (два последних — реальные баги/тонкости, заложенные в код).

**Долг из tests/bench (переформулирую компактно):**
- **РАЗБОР 1.** Почему l2_tests не линкует transport, хотя app линкует всем всё?
- **РАЗБОР 2.** `gtest_discover_tests`: POST_BUILD vs PRE_TEST — что делают, когда первый мешает?
- **РАЗБОР 3.** Property-тесты: зачем и фиксированный seed, и случайный ночной? Что теряет каждый режим поодиночке?
- **РАЗБОР 4.** Фаззер с `-fsanitize=fuzzer,address` линкуется с protocol, собранным без этих флагов: (а) что при этом фаззер реально инструментирует, (б) какие баги в protocol он из-за этого НЕ увидит, (в) как собрать правильно?
- **РАЗБОР 5.** Бенчмарк с L2_CHECK_INVARIANTS=ON: почему числа невалидны не только «замедлением», но и качественно — что именно инварианты делают с кэшем и ветвлениями измеряемого кода?
- **РАЗБОР 6.** Данные бенчмарка: скачиваются скриптом с pinned sha256. Почему отвергнуты (а) хранение в git, (б) синтетическая генерация? Причины разные.

**Порядок работы, предложение:**
1. В-12 и В-19 — на бумаге, до кода (это дизайн сшивки).
2. `types.h` + `MapStorage` + `BookL2` руками → typed-тесты зелёные.
3. `Sequencer` руками с учётом решения В-12/В-19 → таблицы зелёные.
4. `ArrayStorage::recenter` — по спецификации, тесты до кода.
5. Остальное — делегируйте и ревьюйте.

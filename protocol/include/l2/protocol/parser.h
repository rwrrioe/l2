#pragma once
#include <l2/common/types.h>
#include <l2/common/result.h>
#include <l2/protocol/events.h>
#include <variant>
#include <memory>


using ParsedEvent = std::variant<DepthEvent, SnapshotEvent>;

class Parser {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit Parser (TickConverter conv);
    ~Parser();

    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&)  = delete;

    Result<ParsedEvent, ParseError> parse (const Frame& f) noexcept;
};

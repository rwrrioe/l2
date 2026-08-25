#include <l2/protocol/parser.h>
#include <simdjson.h>
#include <cstring>

bool parse_fp8 (std::string_view s, uint64_t& out) noexcept {
    uint64_t ip = 0, fp = 0; int fdig = 0; size_t i = 0;

    if (s.empty()) return false;

    for (; i < s.size() && s[i] != '.'; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        ip = ip * 10 + uint64_t(s[i] - '0');
    }

    if (i < s.size()) {
        for (++i; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            if (fdig < 8) { fp = fp * 10 + uint64_t(s[i] - '0'); ++fdig; }
        }
    }

    while (fdig++ < 8) fp *= 10;
    out = ip * kFpScale + fp;
    return true;
}


struct Parser::Impl {
    simdjson::ondemand::parser sj;
    simdjson::padded_string padded;
    TickConverter conv;
    std::array<LevelUpdate, 1024> bid_buf, ask_buf;

    explicit Impl(TickConverter c) : conv(c) {}

    Result<ParsedEvent, ParseError> parse(const Frame& f) noexcept {
        //todo copy
           padded = simdjson::padded_string(
               reinterpret_cast<const char*>(f.payload.data()), f.payload.size());

           simdjson::ondemand::document doc;
           if (sj.iterate(padded).get(doc)) return err(ParseError::BadJSON);

           if (f.kind == StreamKind::Snapshot) {
               simdjson::ondemand::object root;
               if (doc.get_object().get(root)) return err(ParseError::BadJSON);
               return parse_snapshot(root, f);
           }

           // ws combined stream: {"stream": "...", "data": {...}}
           simdjson::ondemand::object data;
           if (doc["data"].get(data)) return err(ParseError::UnknownStream);

           if (f.kind == StreamKind::Depth) return parse_depth(data, f);
           return err(ParseError::UnknownStream);
       }

       Result<ParsedEvent, ParseError> parse_depth(simdjson::ondemand::object& d,
                                                   const Frame& f) noexcept {
           DepthEvent e{};
           e.rx_timestamp_ns = f.rx_timestamp_ns;
           e.pu = DepthEvent::kNoPu;

           uint64_t tmp;
           if (d["U"].get(e.U) || d["u"].get(e.u)) return err(ParseError::BadJSON);
           if (!d["pu"].get(tmp)) e.pu = tmp;
           uint64_t E;
           if (d["E"].get(E)) return err(ParseError::BadJSON);
           e.event_time_us = E * 1000;                        // ms → us

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
           if (d[key].get(arr)) return ParseError::BadJSON;
           for (auto lvl : arr) {
               if (n == buf.size()) return ParseError::TooManyLevels;
               simdjson::ondemand::array pair;
               if (lvl.get(pair)) return ParseError::BadJSON;
               auto it = pair.begin();
               std::string_view ps, qs;
               if ((*it).get(ps)) return ParseError::BadJSON;
               ++it;
               if ((*it).get(qs)) return ParseError::BadJSON;
               uint64_t pfp, qfp;
               if (!parse_fp8(ps, pfp) || !parse_fp8(qs, qfp))
                   return ParseError::BadDecimal;
               if (!conv.is_valid(pfp)) return ParseError::PriceOffTick;
               buf[n++] = LevelUpdate{conv.to_ticks(pfp), Qty{qfp}};
           }
           return ParseError{};
       }

       Result<ParsedEvent, ParseError> parse_snapshot(simdjson::ondemand::object& d,
                                                      const Frame& f) noexcept {
        SnapshotEvent s{};
        s.rx_timestamp_ns = f.rx_timestamp_ns;

        if(d["lastUpdateId"].get(s.last_update_id)) return err(ParseError::BadJSON);

        size_t nb = 0, na = 0;
        if (fill_levels(d, "bids", bid_buf, nb) != ParseError{} ||
            fill_levels(d, "asks", ask_buf, na) != ParseError{}
        ) return err(ParseError::BadDecimal);

        s.bids = {bid_buf.data(), nb};
        s.asks = {ask_buf.data(), na};

        return Result<ParsedEvent, ParseError>::ok(s);
       }

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

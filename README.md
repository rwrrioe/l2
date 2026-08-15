# l2-engine

Gap-free L2 order book reconstruction for Binance (spot + futures), a price-time-priority matching engine, and microstructure signals (OFI, microprice) — with the emphasis on **provable correctness**: every claim below is backed by a reproducible check, not by assertion.

---

## Architecture

```
WS live ─┐
REST ────┼─► Recorder (raw journal) ─► Parser ─► Sequencer ─► BookL2<Storage> ─► Signals
File ────┘        (bytes first,        (simdjson)  (FSM:        (map | array      (OFI,
                   replay = truth)                  gap-fatal)    | hash)           microprice)
```

Design rules the code enforces:

- **One pipeline = one thread.** No locks on the hot path; SPSC queues only where speeds differ (disk, REST, cross-check). Determinism is a feature, not an accident.
- **Binding time over indirection.** The only virtual call is the cold frame-source boundary (live vs file). Venue policy (spot `U/u` vs futures `pu`), storage backend, and sinks are template parameters — hexagonal ports, statically bound.
- **The book knows nothing about Binance.** `book/` depends only on `common/`; protocol fields die at the sequencer. Adding a venue means adding a policy, not touching verified code.
- **Journal before parse.** Raw frames are recorded ahead of the parser, so the entire pipeline — parser included — is a replayable function of the journal.

## Correctness methodology

- **Gaps are fatal.** A sequence break (`U != prev_u+1` spot; `pu != prev_u` futures) transitions the book to `DESYNCED` and triggers resync. No "log and continue": a silently drifted book looks plausible and poisons everything downstream.
- **Crossed-book detection.** `best_bid >= best_ask` on a diff stream is impossible at the exchange — so it's treated as proof of local corruption.
- **External truth.** A side thread periodically pulls REST snapshots and diffs them against the reconstruction. This is the one check that can't be self-deceived.
- **Differential testing.** The optimized matcher is verified against a deliberately naive `std::map`-based reference on randomized operation streams; trade outputs must match byte-for-byte.
- **Invariants in Release.** `-DL2_CHECK_INVARIANTS=ON` is independent of build type: the primary validation mode is *fast replay with checks on*.
- **Fuzzing.** The parser — the only point where untrusted bytes become typed data — runs under libFuzzer+ASan/UBSan in CI.

## Benchmarks

Methodology (numbers without it are noise):

- Timing: `rdtscp`, invariant-TSC verified (`constant_tsc`, `nonstop_tsc`), calibrated to ns.
- Distribution: HdrHistogram; **tail percentiles reported, never bare means**.
- Attribution: `perf stat` (cache-misses, LLC-load-misses, branch-misses, IPC) explains each latency delta.
- Disclosure: CPU model, governor, turbo state, pinning, isolcpus — printed into every report.
- **Honesty note:** live Binance delivers hundreds–thousands of updates/sec per symbol. Throughput figures are measured on **replayed recorded data**; live runs prove correctness, not speed.

| Storage backend | Throughput (M upd/s) | p50 | p99 | p99.9 | LLC-miss/op |
|---|---|---|---|---|---|
| `std::map` (reference) | TBD | TBD | TBD | TBD | TBD |
| flat array (banded, tick-indexed) | TBD | TBD | TBD | TBD | TBD |
| open-addressing hash | TBD | TBD | TBD | TBD | TBD |

## Signals

- **OFI** (Cont, Kukanov, Stoikov, *J. Financial Econometrics* 2014): computed from best-quote transitions only. The paper's falsifiable claim — price-impact slope β ≈ c/2D — is tested by regressing ΔP on OFI within depth regimes: `TBD` table (β·D constancy), R² by horizon (10ms/100ms/1s/10s).
- **Microprice / weighted mid** (Stoikov 2017): evaluated by markout — `E[mid(t+Δ) − F(t)]` vs Δ for mid / weighted-mid / microprice on one figure: `TBD`. An unbiased fair-value estimator sits flat at zero.
- Analysis is offline (Python/pandas) over CSVs emitted by replay; the hot path computes, it doesn't interpret.

## Build

```bash
cmake --preset dev          # Debug
cmake --preset asan         # ASan+UBSan tests
cmake --preset rel-checked  # Release + invariants (primary replay mode)
cmake --preset hot          # -O3 -march=native -fno-exceptions, benchmarks

ctest --test-dir build-asan
./build-rel/bin/l2_replay data/session.bin <tick_fp> hashes.log
```

Requires: C++20, CMake 3.24+, Linux. Deps pinned via FetchContent (simdjson, xxhash, ankerl, gtest, benchmark); Boost/OpenSSL from system.

## Known limitations

Stated up front because unstated limits are how benchmarks lie:

- **Banded array storage** tracks ±N ticks around mid; levels outside the band are *unknown*, not absent. Deletes outside the band are no-ops by design.
- **Snapshot depth caps** (5000 spot / 1000 futures) bound the visible horizon; far levels can be phantom until touched.
- Live throughput is exchange-limited; performance claims are replay-based (see above).
- Matching engine is L3-simulated (own orders only) — public feeds don't expose per-order identity.
- Binance sequencing rules verified against docs as of **TBD date**; the spot/futures distinction is re-checked before any protocol change.

## References

Cont–Kukanov–Stoikov 2014 · Stoikov, *The Micro-Price* 2017 · Harris, *Trading and Exchanges* · Bouchaud et al., *Trades, Quotes and Prices* · Tene, *How NOT to Measure Latency* · Cook, CppCon 2017 · SQLite, *How SQLite Is Tested* (the testing-culture role model for this project)

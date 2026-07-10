---
name: run-instrument_loader
description: Build, run, and verify instrument_loader — a C++ CLI batch job that fetches tradable-instrument lists from the Binance and Bybit REST APIs and upserts them into PostgreSQL. Use when asked to build, run, test, or verify instrument_loader, or to check that a code change still loads instruments correctly end-to-end.
---

instrument_loader is a one-shot CLI (no flags, no stdin, no server) — it
connects to Postgres, ensures its schema, fetches instrument lists from
two live exchange APIs, upserts them, and exits. There's no UI to
screenshot; "driving" it means running it and checking the database it
wrote to. Do that with the driver:
`.claude/skills/run-instrument_loader/smoke.sh` (paths below are
relative to the repo root, `instrument_loader/`).

## Prerequisites

Already satisfied on this box; on a fresh Ubuntu 24.04 box install:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev \
    nlohmann-json3-dev libpq-dev postgresql
```

`libpqxx` is NOT installed from `apt` — see Gotchas. CMake's
`FetchContent` clones and builds it from source automatically
(~47MB, adds a couple minutes to the *first* configure+build only).

A local PostgreSQL 16 server must be running (`service postgresql
start` if it isn't) with a database named `instruments` owned by a
role that can connect with a password. This box already has that
provisioned (`postgres` role, db `instruments`).

Env vars (all optional — see `common/config/Config.h` for fallbacks):

```bash
export PGHOST=localhost      # default: localhost
export PGPORT=5432           # default: 5432
export PGDATABASE=instruments  # default: instruments
export PGUSER=postgres       # default: postgres
export PGPASSWORD=...        # default: postgres — required in practice,
                              # already exported in ~/.bashrc on this box;
                              # use `bash -i -c '...'` to inherit it, or
                              # export your own.
```

## Build

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Produces `build/instrument_loader`. Both commands are idempotent —
safe to re-run after pulling changes.

## Run (agent path)

Use the driver — it builds, runs one real load, and verifies Postgres
actually changed (row counts alone don't prove anything since the
loader upserts against the same ~4200 rows every time; the driver
checks `updated_at` freshness instead):

```bash
bash -i -c 'PGPASSWORD=$PGPASSWORD .claude/skills/run-instrument_loader/smoke.sh'
```

Expected tail of output:

```
==> instrument rows before: 4223
==> running instrument_loader
Fetching instruments from Binance...
  saved 3625 instruments
Fetching instruments from Bybit...
  saved 598 instruments
==> instrument rows after:  4223 (4223 touched in the last 2 minutes)
PASS: instrument_loader built, ran against live Binance/Bybit APIs, and upserted 4223 rows into Postgres
```

`smoke.sh` exits non-zero if the loader ran but didn't actually write
fresh rows (e.g. it silently failed both connectors and swallowed the
exceptions — `main.cpp` catches per-connector errors and keeps going,
so a broken connector does NOT fail the process on its own).

To inspect the data directly instead of just the pass/fail:

```bash
bash -i -c 'PGPASSWORD=$PGPASSWORD psql -h localhost -U postgres -d instruments -c \
  "SELECT i.native_symbol, s.base_asset, s.quote_asset, i.is_active, i.tick_size
   FROM instrument i JOIN symbol s ON s.id = i.symbol_id
   WHERE i.exchange_name = '"'"'Binance'"'"' LIMIT 5;"'
```

## Run (human path)

Same binary, no driver needed. The `postgres` role's real password on
this box is NOT the `Config.h` fallback (`postgres`) — it's exported
as `PGPASSWORD` in `~/.bashrc` for interactive shells, so run it via
`bash -i`:

```bash
bash -i -c 'PGPASSWORD=$PGPASSWORD ./build/instrument_loader'
```

Prints progress per exchange to stdout and returns exit code 1 only on
a fatal error (e.g. can't connect to Postgres at all) — per-exchange
fetch/parse failures are logged to stderr and skipped, exit code stays 0.

## Test

There is no test suite for this project (no `tests/` directory, no
test target in `CMakeLists.txt`). `smoke.sh` is the only verification
available — it's an integration check against the live exchange APIs
and a real database, not a unit test.

---

## Gotchas

- **`libpqxx-dev` from apt is installed but unused on purpose.**
  `CMakeLists.txt` comments explain: the distro package is built
  without `std::source_location`, which ABI-mismatches this project's
  C++20 build. `FetchContent` builds libpqxx 8.0.1 from source instead.
  Don't "fix" this by pointing `find_package` at the system package —
  it was tried and reverted for that reason.
- **Row counts don't prove the loader worked.** Both connectors fetch
  the full current instrument list from Binance/Bybit and upsert on
  `(exchange_name, native_symbol)`, so a successful run against an
  already-populated DB leaves `count(*)` unchanged. Check
  `updated_at` freshness instead (`smoke.sh` does this).
- **A connector can fail silently from the process's point of view.**
  `main.cpp`'s per-connector `try/catch` means if Bybit's API shape
  changes or the network call times out, you get `failed to load
  Bybit: ...` on stderr but exit code 0 and Binance's data still
  saved. Don't rely on exit code alone to detect a partial failure —
  grep stdout/stderr for `failed to load`.
- **First build is slow, later ones are fast.** The very first
  `cmake --build` clones and compiles libpqxx from source
  (~2 minutes); after that `build/_deps` is cached and rebuilds only
  recompile changed project files.

## Troubleshooting

- **`password authentication failed for user "postgres"`**: wrong or
  unset `PGPASSWORD`. On this box the correct value is exported in
  `~/.bashrc` for interactive shells only — run via
  `bash -i -c '...'` (not plain `bash -c`) so it's inherited, or
  `export PGPASSWORD=...` yourself first.
- **`sudo: a password is required` when trying `sudo -u postgres
  psql`**: don't use `sudo` for DB access here — connect directly
  over TCP as `psql -h localhost -U postgres` with `PGPASSWORD` set;
  password auth is enabled for local TCP connections.
- **Bybit connector throws `API error: ...`**: the endpoint
  (`GET https://api.bybit.com/v5/market/instruments-info?category=spot&limit=1000`)
  returns a JSON body with `retCode` even on HTTP 200; a non-zero
  `retCode` throws in `BybitConnector.cpp`. A bare `curl -I` (HEAD)
  against that URL returns 404 — that's normal, the API only accepts
  GET; don't mistake it for an outage.

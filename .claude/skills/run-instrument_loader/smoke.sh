#!/usr/bin/env bash
# Driver for instrument_loader: builds it (if needed), runs one real load
# against Binance + Bybit into Postgres, then verifies the DB actually
# changed. This is a one-shot batch CLI, not a long-running server, so
# "driving" it means: run it, then query the DB it wrote to.
#
# Usage:
#   .claude/skills/run-instrument_loader/smoke.sh
#
# Required env (all have Config.h fallbacks, see SKILL.md):
#   PGHOST PGPORT PGDATABASE PGUSER PGPASSWORD
set -euo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || echo "$(dirname "${BASH_SOURCE[0]}")/../../..")"

: "${PGHOST:=localhost}"
: "${PGPORT:=5432}"
: "${PGDATABASE:=instruments}"
: "${PGUSER:=postgres}"
: "${PGPASSWORD:?PGPASSWORD must be set (see SKILL.md Prerequisites)}"
export PGHOST PGPORT PGDATABASE PGUSER PGPASSWORD

psql_cmd() { psql -h "$PGHOST" -p "$PGPORT" -U "$PGUSER" -d "$PGDATABASE" -tA -c "$1"; }

echo "==> configuring"
cmake -S . -B build >/dev/null

echo "==> building"
cmake --build build -j"$(nproc)"

before_count=$(psql_cmd "SELECT count(*) FROM instrument" 2>/dev/null || echo 0)
echo "==> instrument rows before: ${before_count}"

echo "==> running instrument_loader"
./build/instrument_loader

after_count=$(psql_cmd "SELECT count(*) FROM instrument")
fresh_count=$(psql_cmd "SELECT count(*) FROM instrument WHERE updated_at > now() - interval '2 minutes'")
echo "==> instrument rows after:  ${after_count} (${fresh_count} touched in the last 2 minutes)"

if [ "$after_count" -eq 0 ] || [ "$fresh_count" -eq 0 ]; then
    echo "FAIL: no rows were freshly written — loader did not actually save data" >&2
    exit 1
fi

echo "PASS: instrument_loader built, ran against live Binance/Bybit APIs, and upserted ${fresh_count} rows into Postgres"

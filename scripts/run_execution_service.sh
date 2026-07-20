#!/usr/bin/env bash
# Loads .env (if present) and runs execution_service. Copy .env.example to
# .env and fill in real testnet keys before running this for anything beyond
# the default "everything rejects with an auth error" behavior.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -f "$repo_root/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "$repo_root/.env"
    set +a
else
    echo "warning: no .env found at $repo_root/.env — running with defaults (empty API keys, every order will be rejected)" >&2
fi

exec "$repo_root/build/execution_service"

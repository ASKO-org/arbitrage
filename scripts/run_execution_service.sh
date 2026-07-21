#!/usr/bin/env bash
# Loads .env (if present) and runs execution_service. Copy .env.example to
# .env for non-secret settings (base URLs, Postgres/Redis overrides). The
# exchange API keys themselves are NOT in .env — see .env.example for the
# secrets_cli setup (encrypted at rest, master key kept outside the repo).
# execution_service refuses to start at all if that secrets store isn't set
# up yet, rather than silently running with empty credentials.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -f "$repo_root/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "$repo_root/.env"
    set +a
else
    echo "warning: no .env found at $repo_root/.env — using defaults for non-secret settings" >&2
fi

exec "$repo_root/build/execution_service"

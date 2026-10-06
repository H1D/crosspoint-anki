#!/usr/bin/env bash
# Deploys the Worker with a token scoped to this one Worker. Such a token
# cannot read the account's workers.dev subdomain, which `wrangler deploy`
# checks, so upload a version and promote it to 100% instead.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CLOUDFLARE_API_TOKEN:?set CLOUDFLARE_API_TOKEN}"
export CLOUDFLARE_ACCOUNT_ID="${CLOUDFLARE_ACCOUNT_ID:-8a97ec747d5396c10e3e0b739d75ef22}"
WRANGLER="${WRANGLER:-npx --yes wrangler@4}"

out="$($WRANGLER versions upload 2>&1 || true)"
version="$(printf '%s\n' "$out" | sed -n 's/.*Worker Version ID: \([0-9a-f-]*\).*/\1/p' | head -1)"
if [ -z "$version" ]; then
  printf '%s\n' "$out" >&2
  echo "upload failed" >&2
  exit 1
fi
$WRANGLER versions deploy "$version@100%" -y >/dev/null
echo "deployed $version"
curl -fsS https://crosspoint-informers.doorcomp.workers.dev/
echo

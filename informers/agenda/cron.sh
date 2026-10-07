#!/usr/bin/env bash
# Hermes cron entry for the agenda informer: install as
# ~/.hermes/scripts/agenda_informer.sh and schedule hourly, e.g. "40 * * * *".
# Stdout is what Hermes delivers: empty on success, a failure notice otherwise.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
exec uv run -q "$HOME/.hermes/scripts/agenda_informer.py" run

#!/usr/bin/env bash
# Hermes cron entry for the school informer. Install as
# ~/.hermes/scripts/school_informer_morning.sh and ..._evening.sh (the slot
# comes from the file name); schedule both DST candidates in UTC:
#   morning "50 5,6 * * *"   evening "0 17,18 * * *"
# The script itself only runs at 07:50 / 19:00 Amsterdam time. Stdout is what
# Hermes delivers: alarms for no school today/tomorrow, or a failure notice.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
case "$(basename "$0")" in
  *morning*) slot=morning ;;
  *evening*) slot=evening ;;
  *) echo "school informer: name this script *_morning.sh or *_evening.sh" >&2; exit 2 ;;
esac
exec uv run -q "$HOME/.hermes/scripts/school_informer.py" run --slot "$slot" --notify

#!/usr/bin/env sh
# Build the working tree and launch it for testing. Arguments go to the app,
# e.g. `./build_and_run.sh ~/Downloads` or `./build_and_run.sh --new-window`.
#
# ROOK_BUILD_TYPE=Release ./build_and_run.sh  for a release-flavoured build.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BINARY="$ROOT/build/rook"
SERVICE="org.omarchy.rook"

# Only the app target: the test suites are slow to compile (use ./bin/test).
"$ROOT/bin/build" --target rook

# A second launch hands its paths to whichever instance already owns the
# D-Bus name and exits, so a running installed copy would hide this build.
if command -v busctl >/dev/null 2>&1 \
  && busctl --user status "$SERVICE" >/dev/null 2>&1; then
  echo "An instance already owns $SERVICE; this build would just forward to it." >&2
  printf "Quit it and launch the new build? [y/N] " >&2
  read -r answer
  case "$answer" in
    [yY]*)
      pkill -x rook || true
      while busctl --user status "$SERVICE" >/dev/null 2>&1; do sleep 0.1; done
      ;;
    *) exit 1 ;;
  esac
fi

exec "$BINARY" "$@"

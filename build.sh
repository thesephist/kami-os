#!/bin/sh
set -eu
KAMI_REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${PLATFORMIO_CORE_DIR:=${XDG_CACHE_HOME:-$HOME/.cache}/kami-os/platformio}"
export PLATFORMIO_CORE_DIR
KAMI_PYTHON=${KAMI_PYTHON:-"$KAMI_REPO_ROOT/.venv/bin/python"}
cd "$KAMI_REPO_ROOT"
KAMI_BUILD_ENV=esp32-s3-menu-n16r8-production
if [ "${1:-}" = --diagnostics ]; then
  KAMI_BUILD_ENV=esp32-s3-menu-n16r8-diagnostics
  shift
fi
exec "$KAMI_PYTHON" -m platformio run -e "$KAMI_BUILD_ENV" "$@"

#!/usr/bin/env bash
set -euo pipefail
layout=${1:-60}
case "$layout" in 60|58) ;; *) echo 'Expected 60 or 58' >&2; exit 2;; esac
: "${ZMK_WORKSPACE:?Set ZMK_WORKSPACE to your initialized ZMK west workspace}"
source_dir=$(cd "$(dirname "$0")" && pwd)
cd "$ZMK_WORKSPACE"
west -z zephyr build -s zmk/app -b stokerjack -d "build/stokerjack-ost4ml5b32a-$layout" -- \
  -DZMK_CONFIG="$source_dir/config" -DZMK_EXTRA_MODULES="$source_dir" -DSHIELD="stokerjack_$layout"

#!/usr/bin/env bash
set -euo pipefail
layout=${1:-60}
case "$layout" in 60|58|studio60) ;; *) echo 'Expected 60, 58 or studio60' >&2; exit 2;; esac
: "${ZMK_WORKSPACE:?Set ZMK_WORKSPACE to your initialized ZMK west workspace}"
source_dir=$(cd "$(dirname "$0")" && pwd)
cd "$ZMK_WORKSPACE"
if [[ "$layout" == studio60 ]]; then
  exec west -z zephyr build -s zmk/app -b stokerjack -d build/stokerjack-studio60 -- \
    -DSHIELD=stokerjack_60 "-DZMK_CONFIG=$source_dir/config" \
    "-DZMK_EXTRA_MODULES=$source_dir" \
    "-DCONFIG_BT_CTLR_PHY_2M=n" \
    "-DSTOKERJACK_MANUAL_STATUS=ON" \
    "-DCONFIG_BT_SETTINGS_CCC_LAZY_LOADING=n" \
    "-DCONFIG_ZMK_STUDIO=y" \
    "-DCONFIG_ZMK_STUDIO_LOCKING=n" \
    "-DSNIPPET=studio-rpc-usb-uart;zmk-usb-logging" \
    "-DKEYMAP_FILE=$source_dir/config/stokerjack_solstice_us.keymap" \
    "-DCONFIG_LOG_BACKEND_UART=y" \
    "-DCONFIG_LOG_BACKEND_RTT=n" \
    "-DCONFIG_RTT_CONSOLE=n" \
    "-DEXTRA_DTC_OVERLAY_FILE=$source_dir/config/stokerjack_cdc_boot.overlay" \
    "-DCONFIG_RETAINED_MEM=y" \
    "-DCONFIG_RETENTION=y" \
    "-DCONFIG_RETENTION_BOOT_MODE=y" \
    "-DCONFIG_ZMK_CDC_ACM_BOOTLOADER_TRIGGER=y" \
    "-DEXTRA_CONF_FILE=$source_dir/config/stokerjack_dya.conf"
fi
west -z zephyr build -s zmk/app -b stokerjack -d "build/stokerjack-ost4ml5b32a-$layout" -- \
  -DZMK_CONFIG="$source_dir/config" -DZMK_EXTRA_MODULES="$source_dir" -DSHIELD="stokerjack_$layout"

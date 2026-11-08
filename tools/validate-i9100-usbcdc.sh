#!/usr/bin/env bash
# Bounded offline driver-policy checks; no device access or cleanup.
set -euo pipefail
if [[ ${I9100_CPU_CAPPED:-0} != 1 ]]; then
    export I9100_CPU_CAPPED=1
    exec taskset -c 0-13 nice -n10 ionice -c2 -n7 bash "$0" "$@"
fi
usb_repo=$(cd "$(dirname "$0")/.." && pwd -P)
usb_out="$usb_repo/out/usbcdc-check-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$usb_out"
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror=implicit-function-declaration \
    -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
    "$usb_repo/tools/test-i9100-usbcdc.c" -o "$usb_out/test-usbcdc" \
    >"$usb_out/build.log" 2>&1 || { cat "$usb_out/build.log"; exit 1; }
"$usb_out/test-usbcdc" 2>&1 | tee "$usb_out/results.log"
printf 'Evidence: %s\n' "$usb_out"

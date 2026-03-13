#!/bin/bash

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <platform>" >&2
    exit 1
fi

platform="$1"

# shellcheck source=tests/qemu/common.sh
source "$(cd "$(dirname "$0")" && pwd)/common.sh"

trap restore_profile EXIT
init_log "$platform" smoke
cd "$PROJECT_ROOT"

case "$platform" in
    esp32c3-qemu)
        setup_esp_idf
        prepare_profile "$platform" demo
        build_target "$platform"
        run_esp_qemu "$platform" 90 12 "/help\n"
        "${SCRIPT_DIR}/assert-log.sh" "$log_file" \
            "rt-claw chat" \
            "Show this help" \
            "Anything else is sent directly to AI."
        ;;
    esp32s3-qemu)
        setup_esp_idf
        prepare_profile "$platform" demo
        build_target "$platform"
        run_esp_qemu "$platform" 180 45 "/help\n"
        "${SCRIPT_DIR}/assert-log.sh" "$log_file" \
            "rt-claw chat" \
            "Show this help" \
            "Anything else is sent directly to AI."
        ;;
    vexpress-a9-qemu)
        build_target "$platform"
        run_vexpress_qemu 60
        "${SCRIPT_DIR}/assert-log.sh" "$log_file" "rt-claw v" "init: ai_engine"
        ;;
    *)
        echo "Unsupported smoke platform: ${platform}" >&2
        exit 1
        ;;
esac

echo "Smoke log: ${log_file}"

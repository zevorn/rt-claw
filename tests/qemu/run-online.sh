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
init_log "$platform" online
cd "$PROJECT_ROOT"

case "$platform" in
    esp32c3-qemu)
        setup_esp_idf
        prepare_profile "$platform" feishu
        build_target "$platform"
        run_esp_qemu "$platform" 120 0
        ;;
    esp32s3-qemu)
        setup_esp_idf
        prepare_profile "$platform" feishu
        build_target "$platform"
        run_esp_qemu "$platform" 240 0
        ;;
    *)
        echo "Unsupported online platform: ${platform}" >&2
        exit 1
        ;;
esac

"${SCRIPT_DIR}/assert-log.sh" "$log_file" \
    "Testing AI connection" \
    "[boot] AI>" \
    "ready to receive messages"

echo "Online log: ${log_file}"

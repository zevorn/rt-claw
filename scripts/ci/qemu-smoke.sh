#!/bin/bash

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <platform> <smoke|online>" >&2
    exit 1
fi

platform="$1"
mode="$2"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ARTIFACT_DIR="${RTCLAW_CI_ARTIFACT_DIR:-${PROJECT_ROOT}/ci-artifacts}"
LOG_FILE="${ARTIFACT_DIR}/${platform}-${mode}.log"

backup_dir=""
profile_platform_dir=""

mkdir -p "$ARTIFACT_DIR"
: > "$LOG_FILE"

restore_profile()
{
    if [ -z "$backup_dir" ] || [ -z "$profile_platform_dir" ]; then
        return
    fi

    if [ -f "${backup_dir}/sdkconfig.defaults" ]; then
        cp "${backup_dir}/sdkconfig.defaults" \
            "${profile_platform_dir}/sdkconfig.defaults"
    else
        rm -f "${profile_platform_dir}/sdkconfig.defaults"
    fi

    if [ -f "${backup_dir}/sdkconfig" ]; then
        cp "${backup_dir}/sdkconfig" "${profile_platform_dir}/sdkconfig"
    else
        rm -f "${profile_platform_dir}/sdkconfig"
    fi

    rm -rf "$backup_dir"
}

prepare_profile()
{
    local target="$1"
    local profile="$2"
    local defaults
    local profile_file

    profile_platform_dir="${PROJECT_ROOT}/platform/${target}"
    defaults="${profile_platform_dir}/sdkconfig.defaults"
    profile_file="${defaults}.${profile}"

    if [ ! -f "$profile_file" ]; then
        echo "Missing profile: $profile_file" >&2
        exit 1
    fi

    backup_dir="$(mktemp -d)"

    if [ -f "$defaults" ]; then
        cp "$defaults" "${backup_dir}/sdkconfig.defaults"
    fi

    if [ -f "${profile_platform_dir}/sdkconfig" ]; then
        cp "${profile_platform_dir}/sdkconfig" "${backup_dir}/sdkconfig"
    fi

    cp "$profile_file" "$defaults"
    rm -f "${profile_platform_dir}/sdkconfig"
}

run_command()
{
    local command="$1"
    local rc=0

    set +e
    bash -lc "$command" >> "$LOG_FILE" 2>&1
    rc=$?
    set -e

    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        cat "$LOG_FILE"
        exit "$rc"
    fi
}

build_target()
{
    local target="$1"
    local rc=0

    set +e
    bash -lc "make ${target}" >> "$LOG_FILE" 2>&1
    rc=$?
    set -e

    if [ "$rc" -ne 0 ]; then
        cat "$LOG_FILE"
        exit "$rc"
    fi
}

assert_patterns()
{
    local pattern

    for pattern in "$@"; do
        if ! grep -Fq "$pattern" "$LOG_FILE"; then
            echo "Missing log pattern: $pattern" >&2
            tail -n 200 "$LOG_FILE" >&2
            exit 1
        fi
    done
}

trap restore_profile EXIT

cd "$PROJECT_ROOT"

case "$platform" in
    esp32c3-qemu|esp32s3-qemu)
        if [ ! -f "$HOME/esp/esp-idf/export.sh" ]; then
            echo "ESP-IDF environment not found at ~/esp/esp-idf/export.sh" >&2
            exit 1
        fi
        # shellcheck source=/dev/null
        source "$HOME/esp/esp-idf/export.sh"
        ;;
esac

case "${platform}:${mode}" in
    esp32c3-qemu:smoke)
        prepare_profile "$platform" demo
        build_target "$platform"
        run_command \
            '( sleep 12; printf "/help\n"; ) | timeout 90s make run-esp32c3-qemu'
        assert_patterns "rt-claw chat" "Show this help" "Anything else is sent directly to AI."
        ;;
    esp32s3-qemu:smoke)
        prepare_profile "$platform" demo
        build_target "$platform"
        run_command \
            '( sleep 45; printf "/help\n"; ) | timeout 180s make run-esp32s3-qemu'
        assert_patterns "rt-claw chat" "Show this help" "Anything else is sent directly to AI."
        ;;
    vexpress-a9-qemu:smoke)
        build_target "$platform"
        run_command 'timeout 60s make run-vexpress-a9-qemu'
        assert_patterns "rt-claw v" "init: ai_engine"
        ;;
    esp32c3-qemu:online)
        prepare_profile "$platform" feishu
        build_target "$platform"
        run_command 'timeout 120s make run-esp32c3-qemu'
        assert_patterns "Testing AI connection" "[boot] AI>" "ready to receive messages"
        ;;
    esp32s3-qemu:online)
        prepare_profile "$platform" feishu
        build_target "$platform"
        run_command 'timeout 240s make run-esp32s3-qemu'
        assert_patterns "Testing AI connection" "[boot] AI>" "ready to receive messages"
        ;;
    *)
        echo "Unsupported combination: ${platform} ${mode}" >&2
        exit 1
        ;;
esac

echo "Smoke log: ${LOG_FILE}"

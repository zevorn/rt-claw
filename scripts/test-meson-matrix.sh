#!/bin/bash
# SPDX-License-Identifier: MIT
#
# test-meson-matrix.sh — Meson option matrix testing on Linux (Tier 4)
#
# Tests all Meson boolean option combinations on Linux native builds.
# No cross-compiler needed. Covers: minimal, full, individual toggles,
# invalid combos, and dependency validation.
#
# Usage:
#   scripts/test-meson-matrix.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

BUILD=build/linux-test
PASS=0
FAIL=0

run_combo() {
    local label="$1"; shift
    echo "--- $label ---"
    rm -rf "$BUILD"
    if meson setup "$BUILD" "$@" > /dev/null 2>&1 && \
       meson compile -C "$BUILD" > /dev/null 2>&1; then
        echo "  PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label"
        FAIL=$((FAIL + 1))
    fi
}

expect_fail() {
    local label="$1"; shift
    echo "--- $label (expect error) ---"
    rm -rf "$BUILD"
    if meson setup "$BUILD" "$@" > /dev/null 2>&1; then
        echo "  FAIL: $label (should have errored but succeeded)"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $label (correctly rejected)"
        PASS=$((PASS + 1))
    fi
}

echo "=== Meson Option Matrix (Linux) ==="
echo ""

# --- Minimal build (all features off) ---
run_combo "minimal (all off)" \
    -Dosal=linux \
    -Dswarm=false -Dsched=false -Dskill=false \
    -Dtool_gpio=false -Dtool_system=false -Dtool_sched=false \
    -Dtool_net=false -Dtool_mouse=false -Dheartbeat=false \
    -Dfeishu=false -Dtelegram=false -Dota=false

# --- Full build (all features on) ---
run_combo "full (all on)" \
    -Dosal=linux \
    -Dswarm=true -Dsched=true -Dskill=true \
    -Dtool_gpio=true -Dtool_system=true -Dtool_sched=true \
    -Dtool_net=true -Dtool_mouse=true -Dheartbeat=true \
    -Dfeishu=true -Dtelegram=true -Dota=true

echo ""
echo "--- Individual toggles (off from default) ---"

# Individual feature toggles: disable one at a time
for opt in swarm skill tool_gpio tool_system tool_net feishu telegram; do
    run_combo "${opt}=false" -Dosal=linux -D${opt}=false
done

# sched=false requires dependents off
run_combo "sched=false (+ deps)" \
    -Dosal=linux -Dsched=false -Dtool_sched=false -Dheartbeat=false

echo ""
echo "--- Individual toggles (on from default-off) ---"

# Enable features that are off by default
run_combo "heartbeat=true" -Dosal=linux -Dheartbeat=true
run_combo "ota=true" -Dosal=linux -Dota=true
run_combo "tool_mouse=true" -Dosal=linux -Dtool_mouse=true
run_combo "voice=true" -Dosal=linux -Dvoice=true
run_combo "linux_web_voice=true" \
    -Dosal=linux -Dvoice=true -Dlinux_web_voice=true
run_combo "linux_local_voice=true" \
    -Dosal=linux -Dvoice=true -Dlinux_local_voice=true
run_combo "linux voice endpoints" \
    -Dosal=linux -Dvoice=true \
    -Dlinux_web_voice=true -Dlinux_local_voice=true

echo ""
echo "--- Invalid / edge-case combinations ---"

# heartbeat without sched should fail at meson setup
expect_fail "heartbeat=true + sched=false" \
    -Dosal=linux -Dheartbeat=true -Dsched=false

# Linux voice endpoints require voice=true.
expect_fail "linux_web_voice=true + voice=false" \
    -Dosal=linux -Dvoice=false -Dlinux_web_voice=true
expect_fail "linux_local_voice=true + voice=false" \
    -Dosal=linux -Dvoice=false -Dlinux_local_voice=true

# All services and tools off (empty shell)
run_combo "empty shell (all services+tools off)" \
    -Dosal=linux \
    -Dswarm=false -Dsched=false -Dskill=false \
    -Dtool_gpio=false -Dtool_system=false -Dtool_sched=false \
    -Dtool_net=false -Dtool_mouse=false -Dheartbeat=false \
    -Dfeishu=false -Dtelegram=false -Dota=false

# Clean up
rm -rf "$BUILD"

echo ""
echo "=== Results ==="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"

if [ "$FAIL" -gt 0 ]; then
    echo "  STATUS: FAILED"
    exit 1
fi

echo "  STATUS: OK"
exit 0

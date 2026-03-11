#!/bin/bash
# SPDX-License-Identifier: MIT
#
# check-patch.sh — run checkpatch.pl on rt-claw project files
#
# Only checks src/ and osal/ directories (app layer and framework layer).
# Skips vendor/, platform/, and other directories.
#
# Usage:
#   scripts/check-patch.sh [--file <file>...]   Check specific files
#   scripts/check-patch.sh [--branch <base>]    Check commits since <base> (default: main)
#   scripts/check-patch.sh [--staged]           Check staged changes
#   scripts/check-patch.sh [--help]             Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CHECKPATCH="$SCRIPT_DIR/checkpatch.pl"
CHECKPATCH_FLAGS="--no-tree"

# Directories to check (app layer + framework layer)
CHECK_DIRS="src/ osal/"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

usage() {
    sed -n '3,11p' "$0" | sed 's/^# \?//'
    exit 0
}

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Filter: keep only files under CHECK_DIRS
filter_files() {
    local f
    while IFS= read -r f; do
        for d in $CHECK_DIRS; do
            case "$f" in
                "$d"*) echo "$f"; break ;;
            esac
        done
    done
}

# --- Mode: check specific files ---
check_files() {
    local ret=0
    for f in "$@"; do
        # Only check files in allowed directories
        local allowed=0
        for d in $CHECK_DIRS; do
            case "$f" in
                "$d"*) allowed=1; break ;;
            esac
        done
        if [ "$allowed" -eq 0 ]; then
            warn "Skipping $f (not in check scope: $CHECK_DIRS)"
            continue
        fi
        if [ ! -f "$PROJECT_ROOT/$f" ]; then
            warn "File not found: $f"
            continue
        fi
        info "Checking $f ..."
        if ! $CHECKPATCH $CHECKPATCH_FLAGS --no-signoff --file "$PROJECT_ROOT/$f"; then
            ret=1
        fi
    done
    return $ret
}

# --- Mode: check commits ---
check_branch() {
    local base="${1:-main}"
    local ancestor
    ancestor=$(git -C "$PROJECT_ROOT" merge-base "$base" HEAD 2>/dev/null || echo "$base")

    local commits
    commits=$(git -C "$PROJECT_ROOT" log --format="%H %s" "$ancestor..HEAD" 2>/dev/null)
    if [ -z "$commits" ]; then
        info "No commits since $base, nothing to check."
        return 0
    fi

    info "Checking commits since $ancestor ..."
    local ret=0

    while IFS= read -r line; do
        local sha="${line%% *}"
        local subject="${line#* }"
        echo ""
        info "Commit: $sha $subject"

        # Get list of changed files and filter
        local files
        files=$(git -C "$PROJECT_ROOT" diff-tree --no-commit-id --name-only -r "$sha" | filter_files)
        if [ -z "$files" ]; then
            info "  (no files in check scope, skipped)"
            continue
        fi

        # Run checkpatch on this commit's diff, filtered to relevant paths
        local diff
        diff=$(git -C "$PROJECT_ROOT" show "$sha" -- $CHECK_DIRS)
        if [ -n "$diff" ]; then
            if ! echo "$diff" | $CHECKPATCH $CHECKPATCH_FLAGS --no-signoff -; then
                ret=1
            fi
        fi
    done <<< "$commits"

    return $ret
}

# --- Mode: check staged changes ---
check_staged() {
    local diff
    diff=$(git -C "$PROJECT_ROOT" diff --cached -- $CHECK_DIRS)
    if [ -z "$diff" ]; then
        info "No staged changes in check scope ($CHECK_DIRS)."
        return 0
    fi
    info "Checking staged changes in $CHECK_DIRS ..."
    if ! echo "$diff" | $CHECKPATCH $CHECKPATCH_FLAGS --no-signoff -; then
        return 1
    fi
    return 0
}

# --- Main ---
cd "$PROJECT_ROOT"

case "${1:-}" in
    --help|-h)
        usage
        ;;
    --file|-f)
        shift
        if [ $# -eq 0 ]; then
            error "No files specified."
            exit 1
        fi
        check_files "$@"
        ;;
    --branch|-b)
        shift
        check_branch "${1:-main}"
        ;;
    --staged|-s)
        check_staged
        ;;
    "")
        # Default: check all source files in scope
        info "Checking all files in: $CHECK_DIRS"
        ret=0
        files=$(find $CHECK_DIRS -type f \( -name '*.c' -o -name '*.h' -o -name '*.c.inc' -o -name '*.h.inc' \) 2>/dev/null | sort)
        if [ -z "$files" ]; then
            info "No source files found."
            exit 0
        fi
        for f in $files; do
            if ! $CHECKPATCH $CHECKPATCH_FLAGS --no-signoff --terse --file "$PROJECT_ROOT/$f"; then
                ret=1
            fi
        done
        exit $ret
        ;;
    *)
        error "Unknown option: $1"
        usage
        ;;
esac

#!/bin/bash

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <log-file> <pattern> [pattern...]" >&2
    exit 1
fi

log_file="$1"
shift

for pattern in "$@"; do
    if ! grep -Fq "$pattern" "$log_file"; then
        echo "Missing log pattern: $pattern" >&2
        tail -n 200 "$log_file" >&2
        exit 1
    fi
done

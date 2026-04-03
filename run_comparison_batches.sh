#!/bin/bash

set -euo pipefail

run_case() {
    local pedca="$1"
    local edca="$2"
    echo "=========================================================="
    echo " Running comparison for P-EDCA=$pedca EDCA=$edca"
    echo "=========================================================="
    ./run_comparison.sh "$pedca" "$edca"
    echo ""
}


# 30 total STAs
run_case 3 27
run_case 9 21
run_case 15 15


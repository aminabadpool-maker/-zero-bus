#!/usr/bin/env bash
set -euo pipefail

target="${1:-test}"
make -C /data/data/com.termux/files/home/zero-bus "$target"

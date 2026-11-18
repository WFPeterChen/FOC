#!/usr/bin/env bash
# Clone the header/source-only GUI deps into external/ (shallow). Run once.
set -e
cd "$(dirname "$0")"
mkdir -p external && cd external
[ -d imgui ]  || git clone --depth 1 https://github.com/ocornut/imgui.git
[ -d implot ] || git clone --depth 1 https://github.com/epezent/implot.git
echo "deps ready: $(ls -d imgui implot)"

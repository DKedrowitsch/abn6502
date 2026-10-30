#!/bin/sh
set -eu

sdk_dir="${1:-third_party/ch32v20x}"
commit="b375bbbc7dfe8642d6fb7c17e6d3207ee1c3c9c1"

if [ -d "$sdk_dir/.git" ]; then
    test "$(git -C "$sdk_dir" rev-parse HEAD)" = "$commit"
    exit 0
fi

mkdir -p "$(dirname "$sdk_dir")"
git clone --filter=blob:none --no-checkout \
    https://github.com/openwch/ch32v20x.git "$sdk_dir"
git -C "$sdk_dir" sparse-checkout init --cone
git -C "$sdk_dir" sparse-checkout set EVT/EXAM/SRC EVT/EXAM/USB/USBD/SimulateCDC
git -C "$sdk_dir" checkout "$commit"

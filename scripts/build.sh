#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=1.94.100
archive="$repo_dir/.cache/libfprint-v$version.tar.gz"
source_dir="$repo_dir/_work/libfprint-v$version"
build_dir="$repo_dir/_work/build"
checksum=edc90e02f330a7595ceaf37f2c6ec32ed43541347fe936d3273b0bb2524fd19c
mkdir -p "$repo_dir/.cache" "$repo_dir/_work"
if [[ ! -f "$archive" ]]; then
    curl -fL --retry 3 --connect-timeout 20 \
        "https://gitlab.freedesktop.org/libfprint/libfprint/-/archive/v$version/libfprint-v$version.tar.gz" \
        -o "$archive.download"
    mv -- "$archive.download" "$archive"
fi
printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status
if [[ ! -f "$source_dir/.oms-overlay-ready" ]]; then
    tar -xzf "$archive" -C "$repo_dir/_work"
    patch -d "$source_dir" -p1 < "$repo_dir/patches/libfprint-v$version-integration.patch"
    touch "$source_dir/.oms-overlay-ready"
fi
mkdir -p "$source_dir/libfprint/drivers/oms"
cp -- "$repo_dir/libfprint/drivers/oms/oms.c" "$source_dir/libfprint/drivers/oms/"
cp -- "$repo_dir/libfprint/drivers/oms/oms-protocol.h" "$source_dir/libfprint/drivers/oms/"
cp -- "$repo_dir/tests/oms-protocol.c" "$source_dir/tests/"
options=(--prefix=/usr --libdir=lib64 -Ddrivers=oms -Ddoc=false
         -Dintrospection=true -Dinstalled-tests=false)
if [[ -f "$build_dir/build.ninja" ]]; then
    meson setup --reconfigure "$build_dir" "$source_dir" "${options[@]}"
else
    meson setup "$build_dir" "$source_dir" "${options[@]}"
fi
meson compile -C "$build_dir"
meson test -C "$build_dir" oms-protocol fpi-device fpi-ssm --print-errorlogs
echo "Built: $build_dir/libfprint/libfprint-2.so.2.0.0"

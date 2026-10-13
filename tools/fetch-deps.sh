#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
drmdir=${1:-"$root/vendor/drm-kmod"}
# Matches graphics/drm-612-kmod's upstream release, pinned for reproducibility.
revision=1d054ae55685c45c23aa86541427dfc72afa9ca1
if [ ! -d "$drmdir" ]; then
    mkdir -p "$(dirname -- "$drmdir")"
    git clone --depth 1 --branch drm_v6.12.110_1 https://github.com/freebsd/drm-kmod.git "$drmdir"
fi
if [ "$(git -C "$drmdir" rev-parse HEAD)" != "$revision" ]; then
    echo "Expected DRM revision $revision; refusing to alter a different checkout." >&2
    exit 1
fi
if [ ! -f "$drmdir/.hypervdrm-patched" ]; then
    if [ -n "$(git -C "$drmdir" status --porcelain)" ]; then
        echo 'Refusing to patch a modified DRM checkout.' >&2
        exit 1
    fi
    patch -d "$drmdir" -p1 < "$root/patches/drm-vmbus.patch"
    : > "$drmdir/.hypervdrm-patched"
fi
echo "Prepared DRM 6.12 in $drmdir"

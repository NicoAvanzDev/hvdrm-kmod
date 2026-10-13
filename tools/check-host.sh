#!/bin/sh
set -eu
sysdir=${1:-/usr/src/sys}
drmdir=${2:-vendor/drm-kmod}
if [ "$(uname -s)" != FreeBSD ]; then
    echo 'Build the kernel module on FreeBSD 15.1 amd64; make check also runs on Linux.' >&2
    exit 1
fi
case $(uname -r) in 15.1-*) ;; *) echo 'This release targets FreeBSD 15.1.' >&2; exit 1 ;; esac
if [ "$(uname -m)" != amd64 ] || [ ! -f "$sysdir/conf/kmod.mk" ]; then
    echo 'FreeBSD amd64 and matching kernel sources are required.' >&2
    exit 1
fi
source_version=$(awk '/^#define[[:space:]]+__FreeBSD_version[[:space:]]/ { print $3; exit }' "$sysdir/sys/param.h")
if [ "$source_version" != "$(uname -K)" ]; then
    echo "Kernel source version $source_version differs from running kernel $(uname -K)." >&2
    exit 1
fi
if [ ! -f "$drmdir/.hypervdrm-patched" ]; then
    echo 'Use make deps to prepare the pinned, patched DRM core.' >&2
    exit 1
fi

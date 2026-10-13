#!/bin/sh
set -eu
action=$1
sysdir=$2
drmdir=$(CDPATH= cd -- "$3" && pwd)
kmoddir=$4
destdir=${5:-}
case "$action" in
all)
    make -C "$drmdir" DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm' SYSDIR="$sysdir"
    make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" KMODDIR="$kmoddir"
    ;;
install)
    if [ -z "$destdir" ] && [ "$(id -u)" -ne 0 ]; then
        echo 'Install with sudo make install.' >&2
        exit 1
    fi
    # The unprivileged build must already exist; installation never downloads source.
    for file in "$drmdir"/dmabuf/dmabuf.ko "$drmdir"/linuxkpi_video/linuxkpi_video.ko "$drmdir"/drm/drm.ko module/hypervdrm.ko; do
        if [ ! -f "$file" ]; then
            echo 'Run make before sudo make install.' >&2
            exit 1
        fi
    done
    make -C "$drmdir" DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm' SYSDIR="$sysdir" KMODDIR="$kmoddir" DESTDIR="$destdir" install
    make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" KMODDIR="$kmoddir" DESTDIR="$destdir" install
    ;;
clean)
    make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" clean
    make -C "$drmdir" DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm' SYSDIR="$sysdir" clean
    ;;
*) echo "Unknown action: $action" >&2; exit 1 ;;
esac

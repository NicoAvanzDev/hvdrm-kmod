#!/bin/sh
set -eu
action=$1
sysdir=$2
drmdir=$(CDPATH= cd -- "$3" && pwd)
kmoddir=$4
case "$action" in
module) make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" KMODDIR="$kmoddir" ;;
core) make -C "$drmdir" DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm' SYSDIR="$sysdir" ;;
install)
    make -C "$drmdir" DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm' SYSDIR="$sysdir" KMODDIR="$kmoddir" install
    make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" KMODDIR="$kmoddir" install
    ;;
clean) make -C module SYSDIR="$sysdir" DRMKMODDIR="$drmdir" clean ;;
*) echo "Unknown action: $action" >&2; exit 1 ;;
esac

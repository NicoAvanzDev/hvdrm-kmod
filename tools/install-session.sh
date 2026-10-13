#!/bin/sh
set -eu
prefix=${1:-/usr/local}
destdir=${2:-}
session_user=${3:-}
if [ -z "$destdir" ] && [ "$(id -u)" -ne 0 ]; then
    echo 'Install with sudo make install.' >&2
    exit 1
fi

install -d "$destdir$prefix/bin" "$destdir$prefix/share/wayland-sessions"
install -m 0555 session/hvdrm-sway "$destdir$prefix/bin/hvdrm-sway"
install -m 0444 session/hvdrm-sway.desktop "$destdir$prefix/share/wayland-sessions/"
install -d "$destdir$prefix/share/hvdrm-kmod"
install -m 0555 session/setup-session.sh "$destdir$prefix/share/hvdrm-kmod/setup-session.sh"
# Staging an installation must not change the running system.
[ -z "$destdir" ] || exit 0
sh session/setup-session.sh "$session_user"

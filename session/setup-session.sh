#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
session_user=${1:-${SUDO_USER:-}}
[ "$(id -u)" -eq 0 ] || { echo 'Run desktop setup as root.' >&2; exit 1; }

if ! command -v sway >/dev/null || ! command -v seatd >/dev/null ||
    ! command -v dbus-run-session >/dev/null; then
    pkg install -y sway seatd dbus
fi
sysrc seatd_enable=YES
if ! service seatd onestatus >/dev/null 2>&1; then service seatd start; fi
current_kmods=$(sysrc -n kld_list 2>/dev/null || true)
case " $current_kmods " in
    *' hypervdrm '*) ;;
    *) sysrc kld_list="${current_kmods:+$current_kmods }hypervdrm" ;;
esac

if [ -n "$session_user" ] && [ "$session_user" != root ]; then
    pw groupmod video -m "$session_user"
else
    echo 'To give your desktop user access: pw groupmod video -m YOUR_USER'
fi
echo 'Installed hvdrm-kmod and the Sway session. Reboot, log in at the VM console, then run hvdrm-sway.'

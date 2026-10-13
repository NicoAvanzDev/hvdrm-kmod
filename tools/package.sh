#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
drmdir=$(CDPATH= cd -- "${1:-vendor/drm-kmod}" && pwd)
sh "$root/tools/check-host.sh" /usr/src/sys "$drmdir"
command -v pkg >/dev/null
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
mkdir -p "$stage/boot/modules" "$stage/usr/local/share/doc/hypervdrm" "$root/dist"
mkdir -p "$stage/usr/local/share/hypervdrm"
for name in dmabuf linuxkpi_video drm; do
    install -m 0555 "$drmdir/$name/$name.ko" "$stage/boot/modules/$name.ko"
done
install -m 0555 "$root/module/hypervdrm.ko" "$stage/boot/modules/hypervdrm.ko"
install -m 0444 "$root/README.md" "$root/LICENSE" "$stage/usr/local/share/doc/hypervdrm/"
install -m 0444 "$root/licenses/GPL-2.0.txt" "$stage/usr/local/share/doc/hypervdrm/"
# Include corresponding source and upstream notices with the binary bundle.
tar -czf "$stage/usr/local/share/hypervdrm/module-source.tar.gz" \
    --exclude='*.o' --exclude='*.ko*' --exclude='*.kld' \
    --exclude='*_if.c' --exclude='*_if.h' --exclude='opt_*.h' \
    --exclude='.depend*' --exclude='export_syms' --exclude='*/@' \
    --exclude='*/machine' --exclude='*/x86' --exclude='*/i386' \
    -C "$root" Makefile README.md LICENSE licenses module include tools tests patches docs
tar -czf "$stage/usr/local/share/hypervdrm/drm-source.tar.gz" \
    --exclude='.git' --exclude='*.o' --exclude='*.ko*' --exclude='*.kld' \
    --exclude='.depend*' --exclude='export_syms' --exclude='*/@' \
    --exclude='*/machine' --exclude='*/x86' --exclude='*/i386' -C "$drmdir" .
abi=$(pkg config ABI)
cat > "$stage/+MANIFEST" <<EOF
name: hypervdrm-experimental
version: "0.1.0"
origin: graphics/hypervdrm-experimental
comment: "Experimental Hyper-V DRM display for FreeBSD 15.1"
desc: "Hyper-V Generation 2 display module and VMBus-aware DRM core. Built for $(freebsd-version -k)."
maintainer: "nobody@localhost"
www: "https://reviews.freebsd.org/D39395"
abi: "$abi"
prefix: "/"
licenselogic: multi
licenses: [BSD2CLAUSE, MIT, GPLv2]
files: {
  "/boot/modules/hypervdrm.ko": "-",
  "/boot/modules/drm.ko": "-",
  "/boot/modules/dmabuf.ko": "-",
  "/boot/modules/linuxkpi_video.ko": "-",
  "/usr/local/share/doc/hypervdrm/README.md": "-",
  "/usr/local/share/doc/hypervdrm/LICENSE": "-",
  "/usr/local/share/doc/hypervdrm/GPL-2.0.txt": "-",
  "/usr/local/share/hypervdrm/module-source.tar.gz": "-",
  "/usr/local/share/hypervdrm/drm-source.tar.gz": "-"
}
scripts: {
  post-install: "kldxref /boot/modules",
  post-deinstall: "kldxref /boot/modules"
}
messages: [{message: "Load hypervdrm manually only after reading /usr/local/share/doc/hypervdrm/README.md. Reboot after replacing a loaded DRM core. Disable autoload before removing this package.", type: install}]
EOF
pkg create -r "$stage" -m "$stage" -o "$root/dist"

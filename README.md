# hvdrm-kmod for FreeBSD 15.1

Experimental, out-of-tree Hyper-V Generation 2 display module for FreeBSD
15.1 amd64. It extends the SynthVID approach in
[D39395](https://reviews.freebsd.org/D39395?id=119805) with a DRM/KMS device
for a Wayland compositor using software rendering.

**Hyper-V hardware validation is pending.** Native FreeBSD builds and a
software-rendered Sway session on the RAM test display pass. This does not
establish that the Hyper-V host displays frames or that VT/input work there. See
[docs/VALIDATION.md](docs/VALIDATION.md) for the recorded checks.

The module provides one virtual connector, one primary plane, atomic
modesetting, page-flip completion events, and mappable XRGB8888 dumb buffers.
The host's preferred resolution is the only exposed mode. Rendering happens
on the guest CPU; buffers are copied into the EFI framebuffer and the host
is notified of damage. A 30 Hz refresh also updates the text console.

This first version requires an EFI XRGB8888 framebuffer large enough for
the selected resolution. Generation 1 VMs, GPU acceleration, hardware
cursors, importing buffers from other graphics drivers, guest suspend/resume, and unloading an
attached device are not implemented. Reboot to remove an attached module.
PRIME export and re-import of this driver's own buffers are supported, as
required by wlroots' software display allocator.
Map those buffers through the DRM device using `MAP_DUMB`; mapping the
exported DMA-BUF fd returns `EOPNOTSUPP` because FreeBSD's native DMA-BUF
shim does not implement the VM mapping bridge.

## Build

Run inside the FreeBSD 15.1 guest. Install the **matching kernel sources**
with the FreeBSD installer or the appropriate PkgBase source package;
`/usr/src/sys` must match the running kernel. Confirm `freebsd-version -kru`
before building. Do not use another release's kernel sources.

```sh
pkg install git
git clone https://github.com/NicoAvanzDev/hvdrm-kmod.git
cd hvdrm-kmod
make deps
make core
make
make check
```

`make deps` downloads the pinned DRM 6.12 release and applies the VMBus and
DMA-BUF fixes. The runtime DRM core must include
these changes. An unmodified `drm-612-kmod` package is insufficient.
The module requires a compatibility symbol from the patched core. If a
stock core is already loaded, `kldload` fails with an undefined
`drm_hyperv_compat_version` symbol; reboot after installing the bundle.
Use `SYSDIR=/path/to/sys` to select a different matching source directory.
Use an **absolute** `DRMKMODDIR=/path/to/drm-kmod` to select a prepared
dependency checkout. `make check` also runs on Linux.

## Install and first load

Keep an SSH session open for the first Hyper-V display test. Inside the
guest, as root:

```sh
make install
```

This installs `hypervdrm.ko` and the patched `drm.ko`, `dmabuf.ko`, and
`linuxkpi_video.ko` under `/boot/modules`. It replaces any existing files
with those names. Do not use `make install` alongside a package that owns
those files: use the package route below or remove that package first.
If a previous DRM core is already loaded, reboot before loading this one.

Configure the preferred resolution from an administrator PowerShell on
the Windows host (replace the example resolution if needed):

```powershell
Set-VMVideo -VMName FreeBSD -ResolutionType Single -HorizontalResolution 1920 -VerticalResolution 1080
```

Then in FreeBSD:

```sh
kldload /boot/modules/hypervdrm.ko
dmesg | tail -50
ls -l /dev/dri/
```

Expect a `hypervdrm0` device, a `DRM/KMS ready` message and a `/dev/dri/cardN`
node. A render node is not expected. If `hvfb` from D39395 is installed,
disable its loading before booting this module: both claim the same device.

Only after manual testing succeeds, append `hypervdrm` to `kld_list` in
`/etc/rc.conf`, preserving existing entries. Prefer rc loading over loader
loading so that the external DRM dependencies are loaded in order.

## Installable package

After building all modules:

```sh
make package
pkg add dist/hypervdrm-experimental-0.1.0.pkg
```

The package bundles the patched DRM dependencies and uses the same module
paths as stock DRM packages. Resolve any file conflict before installation;
avoid forcing it. The package is only for the kernel it was built against.
Rebuild it after a kernel upgrade. It does not enable autoload or start a
compositor. A different GPU driver should not share this experimental core
without separate verification.

## Try Sway

Following the [FreeBSD Wayland handbook](https://docs.freebsd.org/en/books/handbook/wayland/),
install the compositor and seat service as root:

```sh
pkg install sway seatd
sysrc seatd_enable=YES
service seatd start
pw groupmod video -m YOUR_USER
```

Log out and back in so that the group change takes effect. Run from the
guest's local text console as that user, replacing `/dev/dri/card0` with
the node created by `hypervdrm` if there are several devices:

```sh
export XDG_RUNTIME_DIR=/tmp/xdg-runtime-$(id -u)
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export LIBSEAT_BACKEND=seatd
export WLR_BACKENDS=drm,libinput
export WLR_RENDERER=pixman
export WLR_DRM_DEVICES=/dev/dri/card0
sway -d 2> "$HOME/sway-hypervdrm.log"
```

The module has no hardware cursor plane; wlroots must draw a software
cursor. Ensure the Hyper-V keyboard and mouse devices are visible through
evdev. For diagnosis, collect `dmesg`, `freebsd-version -kru`, `kldstat`,
`ls -l /dev/dri /dev/input`, and the Sway log. Start from a local VT;
an ordinary SSH shell does not supply a local console session.

To roll back, remove `hypervdrm` from `kld_list`, reboot, then remove the
experimental package and reinstall the stock DRM package if previously
used. The EFI text console is available without this module.

Protocol provenance and dependency revisions are in [docs/SOURCES.md](docs/SOURCES.md).

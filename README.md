# hvdrm-kmod for FreeBSD 15.1

An experimental Hyper-V Generation 2 display driver for FreeBSD 15.1 amd64,
with a software-rendered Wayland desktop. Based on the SynthVID approach in
[FreeBSD review D39395](https://reviews.freebsd.org/D39395?id=119805).

The DRM dependency is included in this repository, already patched. There
are no dependency clones, submodules, or downloads during the build.

## Build and install

Install the **matching FreeBSD kernel sources** through the installer or
PkgBase first. `/usr/src/sys` must match the running 15.1 kernel; check
`freebsd-version -kru`. Then, in the project directory:

```sh
make
sudo make install
```

`make` builds the driver and all three DRM dependency modules. Installation
puts them in `/boot/modules`, installs the session launcher, installs Sway,
seatd and D-Bus if missing, enables seatd and driver loading at boot, and
adds the user running sudo to the `video` group. It preserves existing
`kld_list` entries. Reboot to use the updated kernel modules and group
membership. Log in at the VM's local console and run:

```sh
hvdrm-sway
```

The launcher selects this driver's actual DRM device, uses the login runtime
directory (or creates a private temporary one), selects software rendering,
and starts a D-Bus session. No XDG or WLR exports are required. It also installs
“Sway (Hyper-V)” in the Wayland session menu for an existing display manager.
Sway's normal configuration and command-line options still work:
`hvdrm-sway -d` enables debug logging. Logs are saved in
`~/.local/state/hvdrm/sway.log` by default, keeping diagnostics off the display
console.

When installing from a root shell instead of sudo, use
`make install SESSION_USER=YOUR_USER` to select the desktop user.
`SYSDIR=/path/to/sys` selects other matching kernel sources.
`DESTDIR=/path/to/stage` stages files without installing packages or changing
services, groups or boot settings.

The bundle installs a patched `drm.ko`, `dmabuf.ko` and `linuxkpi_video.ko`.
If a stock DRM package owns those files, remove that package before source
installation, or resolve the file conflict through the package route below.
A different GPU driver needs separate verification with this patched core.
An already-loaded core remains in memory until reboot; a stock core cannot
satisfy the driver's compatibility symbol.

## Download without Git

GitHub's source archive includes the vendored dependency:

```sh
fetch -o hvdrm-kmod.tar.gz https://github.com/NicoAvanzDev/hvdrm-kmod/archive/refs/heads/main.tar.gz
tar -xzf hvdrm-kmod.tar.gz
cd hvdrm-kmod-main
make
sudo make install
```

For an existing Git checkout, `git pull` followed by the two make commands
is enough. `make clean` removes build outputs from both the driver and the
vendored dependency.

## Hyper-V configuration

Use a Generation 2 VM with an EFI XRGB8888 framebuffer. The host's preferred
resolution is the only exposed mode. If necessary, set it from administrator
PowerShell on the Windows host, then boot the VM:

```powershell
Set-VMVideo -VMName FreeBSD -ResolutionType Single -HorizontalResolution 1920 -VerticalResolution 1080
```

If the separate `hvfb` module from D39395 is installed, disable its loading
before booting this driver: both claim the same device. The driver should
report `hypervdrm0`, `DRM/KMS ready`, and a `/dev/dri/cardN` node.
`sysctl dev.hypervdrm.0.drm_device` reports that node. Ensure the Hyper-V
keyboard and mouse are available through evdev. Keep SSH available during
initial testing.

## Display ownership and current limits

The console draws into its own RAM buffer. While Sway owns the display, VT
output and compositor logs cannot overwrite its framebuffer. Releasing DRM
ownership restores the console; reacquiring it forces a complete repaint.
This addresses the console text overlapping the desktop in the reported
Hyper-V screenshot. The fix has native FreeBSD regression coverage; a new
Hyper-V run is still needed to confirm the visible result.

The driver provides one virtual connector and primary plane, atomic modesetting,
page-flip completion events, and mappable XRGB8888 dumb buffers. Rendering
runs on the guest CPU. It supports PRIME export and re-import of its own
buffers; map them through the DRM fd with `MAP_DUMB`. Mapping a DMA-BUF fd
itself returns `EOPNOTSUPP` because the FreeBSD shim lacks that mapping bridge.

Generation 1 VMs, GPU acceleration, hardware cursors, cross-GPU buffer imports,
guest suspend/resume, and unloading an attached display are not implemented.
Reboot to replace or remove a loaded driver. The EFI framebuffer must be
large enough for the selected resolution.

## Optional package and checks

`make check` runs portable protocol tests with ASan/UBSan and checks script
syntax. Native DRM tests are described in [tests/README.md](tests/README.md).
Recorded results and remaining Hyper-V checks are in
[docs/VALIDATION.md](docs/VALIDATION.md).

After building, `make package` produces `dist/hvdrm-kmod-0.2.0.pkg`, including
all four modules, the launcher, and corresponding vendored source. It is
specific to the kernel it was built against. Install it with `sudo pkg add`
and run `sudo sh /usr/local/share/hvdrm-kmod/setup-session.sh YOUR_USER` once
to prepare the desktop packages, service, group and boot configuration.
Then reboot and run `hvdrm-sway` at the local console.

To roll back, remove `hypervdrm` from `kld_list`, reboot, remove the installed
bundle, and reinstall the stock DRM package if previously used. For a source
installation, the bundle files are the four modules above,
`/usr/local/bin/hvdrm-sway` and
`/usr/local/share/wayland-sessions/hvdrm-sway.desktop`. The firmware text
console is available without this driver.

Dependency revision and licenses: [vendor/README.md](vendor/README.md).
Protocol provenance: [docs/SOURCES.md](docs/SOURCES.md).

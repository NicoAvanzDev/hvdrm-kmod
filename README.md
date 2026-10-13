# hvdrm-kmod for FreeBSD 15.1

An experimental Hyper-V Generation 2 DRM/KMS display driver for FreeBSD
15.1 amd64. Based on the SynthVID approach in
[FreeBSD review D39395](https://reviews.freebsd.org/D39395?id=119805).

This project builds and installs kernel modules. Compositors and desktop
sessions are managed through the normal FreeBSD packages and login setup.
The patched DRM dependency is vendored; building needs no dependency clone,
submodule, or network connection.

## Build and install

Install the **matching FreeBSD kernel sources** through the installer or
PkgBase first. `/usr/src/sys` must match the running 15.1 kernel; check
`freebsd-version -kru`. In the project directory:

```sh
make
sudo make install
```

`make` builds `hypervdrm.ko`, `drm.ko`, `dmabuf.ko`, and `linuxkpi_video.ko`.
Installation copies these four modules into `/boot/modules` and updates the
kernel linker hints. It does not install desktop packages, add launchers,
change groups or services, edit login settings, or change boot configuration.

Reboot after replacing loaded modules. To load the driver for a first test:

```sh
sudo kldload hypervdrm
```

For loading on later boots, add `hypervdrm` to your normal `kld_list` in
`/etc/rc.conf`, preserving its existing entries. A stock or previously loaded
DRM core remains in memory until reboot. This driver requires the compatibility
symbol in the bundled patched core.

`SYSDIR=/path/to/sys` selects other matching kernel sources.
`DESTDIR=/path/to/stage` stages the module files. `make clean` cleans all four
module builds. If a stock DRM package owns the module paths above, remove that
package before source installation, or resolve the conflict through the
package route below. A different GPU driver needs separate verification with
this experimental core.

## Download without Git

GitHub's source archive includes the vendored dependency:

```sh
fetch -o hvdrm-kmod.tar.gz https://github.com/NicoAvanzDev/hvdrm-kmod/archive/refs/heads/main.tar.gz
tar -xzf hvdrm-kmod.tar.gz
cd hvdrm-kmod-main
make
sudo make install
```

For an existing checkout, use `git pull` and the same two make commands.

## Wayland compatibility

Use your chosen compositor through its normal FreeBSD package, login session
and seat service. The [FreeBSD Wayland handbook](https://docs.freebsd.org/en/books/handbook/wayland/)
describes that system setup. The kernel module does not supply a desktop.

The driver exposes a standard DRM/KMS card, dumb buffers and atomic modesetting.
Device discovery and the DRM/libinput backends work automatically with the
tested Sway 1.12 / wlroots 0.20.2 packages. FreeBSD's local login PAM session
supplies `XDG_RUNTIME_DIR`; an SSH or `su` session can behave differently.

**Automatic software renderer selection is currently blocked by a userspace
compatibility issue.** FreeBSD libdrm 2.4.134 synthesizes a render-node name for
this KMS-only device even though the node does not exist. wlroots then skips
its automatic pixman fallback. With those package versions, the verified
workaround is just:

```sh
WLR_RENDERER=pixman sway
```

No driver-specific launcher, `WLR_DRM_DEVICES`, `WLR_BACKENDS`, or
`LIBSEAT_BACKEND` setting is needed in the tested single-display setup.
Plain `sway` still needs that libdrm/wlroots detection issue fixed; installing
a kernel module cannot correct a userspace library's renderer-selection logic.
See [docs/WAYLAND.md](docs/WAYLAND.md) for the diagnosis and test details.
Other compositors must support software rendering and this driver's buffer
interfaces; compatibility with every Wayland desktop has not been established.

## Hyper-V configuration

Use a Generation 2 VM with an EFI XRGB8888 framebuffer. The host's preferred
resolution is the only exposed mode. If necessary, set it from administrator
PowerShell on the Windows host, then boot the VM:

```powershell
Set-VMVideo -VMName FreeBSD -ResolutionType Single -HorizontalResolution 1920 -VerticalResolution 1080
```

If `hvfb` from D39395 is installed, disable its loading before booting this
driver: both claim the same device. Expect `hypervdrm0`, `DRM/KMS ready`,
and a `/dev/dri/cardN` node. `sysctl dev.hypervdrm.0.drm_device` reports
that node. Ensure the Hyper-V keyboard and mouse are available through evdev.
Keep SSH available during initial testing.

## Display ownership and current limits

The text console draws into its own RAM buffer. While a compositor owns the
display, console output cannot overwrite its framebuffer. Releasing DRM
ownership restores the console; reacquiring it forces a complete repaint.
This addresses the console text overlapping the desktop in the reported
Hyper-V screenshot. Native regression coverage passes; a new Hyper-V run is
still needed to confirm the visible fix and VT switching.

The driver provides one virtual connector and primary plane, atomic modesetting,
page-flip completion events, and mappable XRGB8888 dumb buffers. Rendering
runs on the guest CPU. PRIME export and re-import of its own buffers are
supported; map them through the DRM fd with `MAP_DUMB`. Mapping a DMA-BUF fd
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

`make package` produces `dist/hvdrm-kmod-0.2.1.pkg`, containing the four kernel
modules, documentation, licenses and corresponding vendored source. Install
with `sudo pkg add`. The package is specific to the kernel it was built against;
rebuild after a kernel upgrade. Package installation updates linker hints and
leaves desktop and boot configuration to the administrator.

Upgrading the 0.2.0 package removes its retired Sway launcher and session files.
If you installed 0.2.0 from source, those files can be removed separately:
`/usr/local/bin/hvdrm-sway`,
`/usr/local/share/wayland-sessions/hvdrm-sway.desktop`, and
`/usr/local/share/hvdrm-kmod/setup-session.sh`. Existing desktop packages,
seatd configuration and group membership remain part of your system setup.

To roll back, remove `hypervdrm` from `kld_list`, reboot, remove the installed
bundle, and reinstall the stock DRM package if previously used. The firmware
text console is available without this driver.

Dependency revision and licenses: [vendor/README.md](vendor/README.md).
Protocol provenance: [docs/SOURCES.md](docs/SOURCES.md).

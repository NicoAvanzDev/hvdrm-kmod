# Validation

Target: FreeBSD 15.1-RELEASE amd64; DRM 6.12 at
`1d054ae55685c45c23aa86541427dfc72afa9ca1`.

- Protocol parser, truncated packets, invalid resolution lists, oversized
  length fields, and framebuffer arithmetic: pass with ASan/UBSan on Linux.
- Shell syntax: pass, including the installed session launcher and setup helper.
- Native FreeBSD build of `hypervdrm`, `drm`, `dmabuf`, and `linuxkpi_video`:
  pass on 15.1-RELEASE GENERIC (`releng/15.1-n283562-96841ea08dcf`).
- Literal `make` as an ordinary user followed by `sudo make install`: pass
  in the disposable QEMU guest, including module installation, session files,
  video group membership, seatd service setup and preserved boot settings.
- Module loading without a matching device: pass, with all dependencies
  loaded by the FreeBSD kernel linker.
- The real KMS/GEM implementation on the RAM fixture: pass for GEM mmap,
  PRIME export and re-import across DRM fds, safe rejection of unsupported
  DMA-BUF fd mmap, invalid framebuffer bounds rejection, legacy modesetting and page flips,
  userspace atomic TEST_ONLY/nonblocking commits, scanout pixel checks, and
  completion events. Console writes and periodic refresh stay isolated while
  a DRM master owns scanout; dropping master restores console pixels, and
  reacquiring it forces a full repaint despite a one-pixel damage clip.
- Package creation, installation with `pkg add`, dependency autoload, and
  unloading without a matching display: pass in the QEMU guest.
- Loading against a core without the compatibility API: rejected by the
  kernel linker with an undefined `drm_hyperv_compat_version` symbol.
- Software-rendered Wayland on the RAM fixture: pass. Sway 1.12 and
  wlroots 0.20.2 enable `Virtual-1` at 1920x1080 using pixman. A swaybg
  Wayland client draws `#112233`; changing it through Sway IPC to `#445566`
  changes both the first and last scanout pixels to `0xff445566`. The active
  output is recorded in [validation/sway-outputs.json](validation/sway-outputs.json)
  and the pixel checks in [validation/scanout.txt](validation/scanout.txt).
- Vendored dependency: 296 files checked against the pinned upstream commit;
  only the four files documented by `patches/drm-vmbus.patch` differ. A fresh
  build from this subset passes on FreeBSD 15.1 without dependency downloads.
- Installed `hvdrm-sway` launcher: pass on the RAM fixture as an ordinary user.
  With runtime and D-Bus variables unset, it creates a user-owned mode-0700
  temporary runtime directory and a working D-Bus session. A Wayland client
  changes scanout from `#112233` to `#445566`. Diagnostics go to the user's
  state directory, and the temporary runtime directory is removed on exit.
  The fixture-only test selects the RAM card and uses unbound seatd because
  QEMU cannot supply a Hyper-V console. Production uses the driver's reported
  device and the regular seatd service.
- Version 0.2.0 package: creation, installation and `pkg check -s` pass. The
  package is approximately 2.36 MiB unpacked, including corresponding source.
- `make install DESTDIR=...`: pass; stages modules and session files without
  changing `/etc/rc.conf` or `/etc/group`.
- User-supplied Hyper-V screenshot (2026-10-13): Sway starts and a foot window
  is visible, with console text overwriting the desktop. The private console
  buffer and DRM ownership fix has passed the native regression above; its
  visible result and VT/input behavior still need a new Hyper-V run.

A QEMU build guest cannot validate Hyper-V synthetic video. The Wayland
check verifies software rendering and DRM presentation to RAM; it does not
verify host negotiation, physical scanout in VMConnect, or Hyper-V input.

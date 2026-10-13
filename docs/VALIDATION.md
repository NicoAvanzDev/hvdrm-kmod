# Validation

Target: FreeBSD 15.1-RELEASE amd64; DRM 6.12 at
`1d054ae55685c45c23aa86541427dfc72afa9ca1`.

- Protocol parser, truncated packets, invalid resolution lists, oversized
  length fields, and framebuffer arithmetic: pass with ASan/UBSan on Linux
  and FreeBSD.
- Shell syntax: pass.
- Native FreeBSD build of `hypervdrm`, `drm`, `dmabuf`, and `linuxkpi_video`:
  pass on 15.1-RELEASE GENERIC (`releng/15.1-n283562-96841ea08dcf`). A regular
  user can build all four modules with the default `make` target.
- Kernel-only `make install`: pass. Only the modules and linker hints are
  installed. Hashes of `/etc/rc.conf`, `/etc/group`, `/etc/pam.d/login`, and
  `/etc/pam.d/system` are unchanged. No compositor or desktop session is
  installed or configured.
- Module loading without a matching device: pass, with dependencies loaded
  by the FreeBSD kernel linker.
- The real KMS/GEM implementation on the RAM fixture: pass for GEM mmap,
  PRIME export and re-import across DRM fds, safe rejection of unsupported
  DMA-BUF fd mmap, invalid framebuffer bounds rejection, legacy modesetting
  and page flips, atomic TEST_ONLY/nonblocking commits, scanout pixels and
  completion events.
- Console isolation: pass. Simulated VT writes and periodic refresh leave
  scanout unchanged during compositor ownership. Dropping DRM master restores
  console pixels; reacquiring it forces a full repaint despite a one-pixel
  damage clip.
- Loading against a core without the compatibility API: safely rejected by
  the kernel linker with an undefined `drm_hyperv_compat_version` symbol.
- Vendored dependency: 296 files checked against the pinned upstream commit;
  only the four files documented by `patches/drm-vmbus.patch` differ. A fresh
  build from this subset passes without dependency downloads.
- Stock Sway discovery: pass with Sway 1.12 / wlroots 0.20.2_1. A normal
  `login(1)` PAM session supplies `/var/run/xdg/swaytest`. Sway discovers the
  DRM card, uses the default seatd/libseat backend and initializes libinput
  with evdev keyboard/mouse devices without XDG exports, `WLR_DRM_DEVICES`,
  `WLR_BACKENDS`, or `LIBSEAT_BACKEND` overrides.
- Automatic renderer selection: fails with stock libdrm 2.4.134,1, which
  returns a render-node path even though that node is absent. wlroots skips
  its automatic pixman fallback. See [WAYLAND.md](WAYLAND.md).
- Direct `WLR_RENDERER=pixman sway`: pass on the RAM fixture. `Virtual-1`
  runs at 1920x1080, and a Wayland wallpaper client changes scanout from
  `#112233` to `#445566`; both first and last visible pixels become
  `0xff445566`. Simulated console output leaves these pixels unchanged while
  Sway is running. No project-specific launcher is used.
- Version 0.2.1 package: creation, upgrade from 0.2.0, and `pkg check -s` pass.
  Its manifest includes the four modules, documentation, licenses and source;
  the retired launcher, session menu entry and setup helper are removed on
  upgrade. The source archive contains the vendored kernel dependency and
  no desktop installer.
- `make install DESTDIR=...`: pass for staging the module files.
- User-supplied Hyper-V screenshot (2026-10-13): Sway starts and a foot window
  is visible, with console text overwriting the desktop. The private console
  buffer and DRM ownership fix has passed native regression coverage; its
  visible result and Hyper-V VT/input behavior still need a new VM run.

The QEMU RAM fixture uses production GEM/KMS code unchanged. Its seatd test
service has VT binding disabled because it has no physical Hyper-V display;
that setting is confined to the disposable test VM. It verifies userspace
hardware discovery, software rendering and presentation to RAM. It does not
establish Hyper-V host negotiation, VMConnect scanout, VT switching or input.

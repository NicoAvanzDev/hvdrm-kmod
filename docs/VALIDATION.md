# Validation

Target: FreeBSD 15.1-RELEASE amd64; DRM 6.12 at
`1d054ae55685c45c23aa86541427dfc72afa9ca1`.

- Protocol parser, truncated packets, invalid resolution lists, oversized
  length fields, and framebuffer arithmetic: pass with ASan/UBSan on Linux.
- Shell syntax: pass.
- Native FreeBSD build of `hypervdrm`, `drm`, `dmabuf`, and `linuxkpi_video`:
  pass on 15.1-RELEASE GENERIC (`releng/15.1-n283562-96841ea08dcf`).
- Installation into `/boot/modules`: pass in the disposable QEMU guest.
- Module loading without a matching device: pass, with all dependencies
  loaded by the FreeBSD kernel linker.
- The real KMS/GEM implementation on the RAM fixture: pass for GEM mmap,
  PRIME export and re-import across DRM fds, safe rejection of unsupported
  DMA-BUF fd mmap, invalid framebuffer bounds rejection, legacy modesetting and page flips,
  userspace atomic TEST_ONLY/nonblocking commits, scanout pixel checks, and
  completion events.
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
- Hyper-V host negotiation, visible scanout, VT switching, and Sway session:
  pending access to the Hyper-V guest.

A QEMU build guest cannot validate Hyper-V synthetic video. The Wayland
check verifies software rendering and DRM presentation to RAM; it does not
verify host negotiation, physical scanout in VMConnect, or Hyper-V input.

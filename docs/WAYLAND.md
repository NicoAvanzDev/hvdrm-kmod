# Wayland integration

hvdrm-kmod installs kernel modules and exposes the ordinary DRM/KMS interface.
It does not install or configure a compositor, renderer library, login session,
seat service, D-Bus session, or runtime directory. A desktop must already have
its standard FreeBSD session setup.

## Verified stock compositor behavior

Tested on FreeBSD 15.1-RELEASE amd64 with Sway 1.12, wlroots 0.20.2_1,
libdrm 2.4.134,1, libudev-devd 0.7.0 and seatd 0.9.3_1.

A regular user's `login(1)` PAM session creates and exports
`XDG_RUNTIME_DIR=/var/run/xdg/USER`. The test invokes the unmodified `sway`
binary directly, without the project's former launcher. Its configuration only
disables Xwayland and sets a solid background so the scanout can be checked.

- The default libseat backend connects to the existing seatd service.
- Default GPU enumeration discovers `/dev/dri/card0` and opens the real driver's
  KMS implementation on the RAM fixture, without `WLR_DRM_DEVICES`.
- The default DRM and libinput backends initialize and detect the guest's evdev
  keyboard and mouse, without `WLR_BACKENDS` or `LIBSEAT_BACKEND`.
- `WLR_RENDERER=pixman sway` presents a 1920x1080 Wayland output and updates
  the framebuffer when a Wayland wallpaper client changes color.
- Plain `sway` fails during automatic renderer selection with these package
  versions, as described below.

The RAM fixture uses the production GEM/KMS code unchanged. It has no Hyper-V
transport or physical console; the test seatd daemon therefore runs with VT
binding disabled. That fixture setting is not part of the kernel installer or
the real Hyper-V desktop setup. A QEMU test establishes userspace discovery,
software rendering and presentation to RAM, rather than Hyper-V VT/input or
VMConnect output.

## Why automatic pixman selection fails

The driver advertises KMS and dumb buffers, and correctly omits `DRIVER_RENDER`.
It exposes a primary card node and has no GPU render node.

In libdrm 2.4.134, the FreeBSD implementation of `drmGetMinorNameForFD()`
computes a path such as `/dev/dri/renderD128` and returns it without checking
whether the corresponding render node exists. Consequently,
`drmGetRenderDeviceNameFromFd()` returns a non-NULL path for this software
KMS display. This behavior is in the
[upstream libdrm source](https://gitlab.freedesktop.org/mesa/drm/-/blob/libdrm-2.4.134/xf86drm.c).

wlroots 0.20.2's `has_render_node()` treats that non-NULL path as evidence of a
render node. Its renderer auto-selection tries GLES2 and Vulkan, then skips
pixman because it believes a hardware render node exists. See the
[upstream wlroots renderer](https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/0.20.2/render/wlr_renderer.c).
The observed stock Sway log contains:

```text
Found 1 GPUs
Initializing DRM backend for /dev/dri/card0 (hypervdrm)
Failed to open DRM node '/dev/dri/renderD128': No such file or directory
Failed to create a GLES2 renderer. Skipping!
Failed to create a Vulkan renderer. Skipping!
Could not initialize renderer
```

The kernel correctly advertises its capabilities; pretending to have a GPU
render node would not implement the OpenGL/Vulkan functionality userspace
expects. An upstream userspace fix must make render-node detection reflect the
actual device before plain `sway` can select pixman automatically on this stack.
The minimal verified workaround is `WLR_RENDERER=pixman sway`.

## Testing the proposed libdrm fix

`patches/libdrm-render-node.patch` targets libdrm 2.4.134 (`xf86drm.c`). The
FreeBSD branch of `drmGetMinorNameForFD()` then returns NULL unless the
synthesized name is an existing DRM character device of the requested type,
matching the Linux branch. Devices with a real render node are unaffected.
It is not yet validated on FreeBSD.

Build the patched port from a ports tree whose `graphics/libdrm` is 2.4.134:

```sh
sudo mkdir -p /usr/ports/graphics/libdrm/files
sudo cp patches/libdrm-render-node.patch \
    /usr/ports/graphics/libdrm/files/patch-xf86drm.c
cd /usr/ports/graphics/libdrm
sudo make reinstall clean
```

Unset `WLR_RENDERER` and start plain `sway -d 2> sway.log`. The log should
show the pixman renderer and no `Failed to open DRM node
'/dev/dri/renderD128'` line. To return to the stock library, remove
`files/patch-xf86drm.c` and run `sudo pkg install -f libdrm`.

## Other compositors

Wayland is a protocol, rather than a common renderer-selection mechanism.
A compositor must support software rendering to a KMS dumb-buffer allocator
and the available XRGB8888 scanout format. GPU acceleration and cross-device
DMA-BUF imports are absent, so success with Sway does not establish that GNOME,
KDE, Wayfire, or every wlroots compositor starts unchanged.

Standard FreeBSD desktop/session setup is described in the
[FreeBSD Wayland handbook](https://docs.freebsd.org/en/books/handbook/wayland/).

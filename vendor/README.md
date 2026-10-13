# Vendored DRM source

`drm-kmod` contains the source needed to build `dmabuf.ko`,
`linuxkpi_video.ko`, and `drm.ko`. Building requires no dependency clone,
submodule, patch download, or network connection.

Upstream: https://github.com/freebsd/drm-kmod
Tag: `drm_v6.12.110_1`
Commit: `1d054ae55685c45c23aa86541427dfc72afa9ca1`

The retained upstream paths are `Makefile`, `README.md`, `compiler_flags.mk`,
`kconfig.mk`, `linuxkpi_version.mk`, `dmabuf/`, `drm/`, `linuxkpi_video/`,
`include/`, `linuxkpi/`, and `drivers/`, excluding the unrelated
`drivers/gpu/drm/{amd,i915,radeon}/` GPU implementations. The upstream
top-level Makefile is used with `DEVELOPER=1 KMODS='dmabuf linuxkpi_video drm'`.

`patches/drm-vmbus.patch` is already applied. All other retained source files
are unchanged from that commit. Original per-file copyright and license
notices are retained; the GPL v2 text is in `licenses/GPL-2.0.txt`.

To update the vendor snapshot, export those paths from the chosen upstream
commit, exclude the GPU directories above, and apply the reviewed core
patch. Update this revision record, then build and test all four modules
against the matching FreeBSD kernel sources before publishing.

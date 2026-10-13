The SynthVID wire protocol and native VMBus integration are based on Yuri
Pankov's FreeBSD framebuffer review, D39395, diff 119805 (April 2023):
https://reviews.freebsd.org/D39395?id=119805
https://reviews.freebsd.org/D39395?download=true&id=119805

The original Microsoft copyright and BSD redistribution terms are retained
in LICENSE and module/hv_vmbus.c. Packet validation, request timeouts, DRM
objects, native RAM buffers, build tooling, and tests are implemented here.

The Linux Hyper-V DRM driver was consulted for protocol behavior, cursor
hiding, display notifications, and the use of software rendering:
https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/hyperv

The external DRM core is FreeBSD drm-kmod, DRM 6.12:
https://github.com/freebsd/drm-kmod/tree/drm_v6.12.110_1
Commit: 1d054ae55685c45c23aa86541427dfc72afa9ca1

patches/drm-vmbus.patch removes PCI assumptions from DRM node creation and
the FreeBSD DRM sysctl bus ID. It also initializes the native DMA-BUF shim's
VM descriptor so that unsupported DMA-BUF mappings can be rejected safely.
An exported compatibility function ensures hypervdrm cannot bind to an
unmodified or already-loaded stock DRM core.
It must be applied to the core used at runtime,
not just the headers used to compile hypervdrm.ko.

FreeBSD 15.1 kernel sources:
https://github.com/freebsd/freebsd-src/tree/releng/15.1

Wayland setup reference:
https://docs.freebsd.org/en/books/handbook/wayland/

`make check` runs the portable protocol tests under ASan/UBSan.

To exercise the real GEM and KMS implementation on a disposable FreeBSD
15.1 guest without Hyper-V hardware, build and install the patched DRM
core, then run as root:

```sh
pkg install libdrm
make -C tests/kernel
kldload ./tests/kernel/hypervdrm_fixture.ko
cc -Wall -Wextra -Wno-unused-parameter -I/usr/local/include -I/usr/local/include/libdrm \
  tests/kms_test.c -L/usr/local/lib -ldrm -o /tmp/hypervdrm-kms-test
/tmp/hypervdrm-kms-test /dev/dri/card0
```

The fixture registers a RAM display on nexus and reuses `module/hv_kms.c`
unchanged. It never claims the EFI console, opens VMBus, or reaches the
Hyper-V host. It is excluded from binary packages. Reboot the disposable
guest to remove the fixture; do not load it on the intended desktop VM.

The client tests userspace buffer mapping, PRIME export and re-import,
rejection of unsupported DMA-BUF fd mappings, malformed framebuffer offsets,
software scanout, legacy and atomic page flips, and delivery of completion
events. Hyper-V transport, visible output, VT switching, and a real Wayland
session still require a Hyper-V guest.

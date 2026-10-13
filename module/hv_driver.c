/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <machine/metadata.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include "hv_drm.h"
#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include "vmbus_if.h"
#include "fb_if.h"

/* A stock drm.ko cannot resolve this symbol and is rejected by the linker. */
extern int drm_hyperv_compat_version(void);

static const struct hyperv_guid hv_video_guid = { .hv_guid = {
	0x02, 0x78, 0x0a, 0xda, 0x77, 0xe3, 0xac, 0x4a,
	0x8e, 0x77, 0x05, 0x58, 0xeb, 0x10, 0x73, 0xf8
} };

static int
hv_drm_probe(device_t dev)
{
	if (resource_disabled("hypervdrm", device_get_unit(dev)) ||
	    VMBUS_PROBE_GUID(device_get_parent(dev), dev, &hv_video_guid) != 0)
		return (ENXIO);
	device_set_desc(dev, "Hyper-V DRM synthetic display");
	return (BUS_PROBE_DEFAULT);
}

static struct fb_info *
hv_drm_getinfo(device_t dev)
{
	struct hv_drm_softc *sc = device_get_softc(dev);
	return (&sc->console);
}

static int
hv_drm_attach(device_t dev)
{
	struct hv_drm_softc *sc = device_get_softc(dev);
	const struct efi_fb *efi;
	caddr_t metadata;
	int error;

	sc->dev = dev;
	if (drm_hyperv_compat_version() != 1)
		return (ENXIO);
	metadata = preload_search_by_type("elf kernel");
	if (metadata == NULL)
		metadata = preload_search_by_type("elf64 kernel");
	efi = metadata == NULL ? NULL : (const void *)preload_search_info(metadata,
	    MODINFO_METADATA | MODINFOMD_EFI_FB);
	if (efi == NULL || efi->fb_addr == 0 || efi->fb_size == 0 ||
	    efi->fb_size > INT_MAX || efi->fb_addr > UINT64_MAX - efi->fb_size ||
	    efi->fb_mask_red != 0x00ff0000 || efi->fb_mask_green != 0x0000ff00 ||
	    efi->fb_mask_blue != 0x000000ff) {
		device_printf(dev, "requires a Generation 2 EFI XRGB8888 framebuffer\n");
		return (ENXIO);
	}
	sc->vram_address = efi->fb_addr;
	sc->vram_size = efi->fb_size;
	sc->width = efi->fb_width;
	sc->height = efi->fb_height;
	error = hv_bus_start(sc);
	if (error != 0)
		goto fail;
	if (sc->width > INT_MAX / 4 || sc->height > INT_MAX) {
		error = EINVAL;
		goto fail;
	}
	sc->pitch = sc->width * 4;
	if (!hv_surface_fits(sc->width, sc->height, sc->pitch, sc->vram_size)) {
		device_printf(dev, "%ux%u exceeds EFI VRAM (%ju bytes)\n",
		    sc->width, sc->height, (uintmax_t)sc->vram_size);
		error = ENOSPC;
		goto fail;
	}
	sc->vram = pmap_mapdev_attr(sc->vram_address, sc->vram_size,
	    VM_MEMATTR_WRITE_COMBINING);
	if (sc->vram == NULL) {
		error = ENOMEM;
		goto fail;
	}
	/* VT must never draw over a compositor's scanout, even during log output. */
	sc->console_buffer = malloc((size_t)sc->pitch * sc->height,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	error = hv_host_configure(sc);
	if (error != 0)
		goto fail;
	sc->console.fb_type = FBTYPE_PCIMISC;
	sc->console.fb_width = sc->width;
	sc->console.fb_height = sc->height;
	sc->console.fb_depth = sc->console.fb_bpp = 32;
	sc->console.fb_stride = sc->pitch;
	sc->console.fb_size = (size_t)sc->pitch * sc->height;
	sc->console.fb_pbase = 0;
	sc->console.fb_vbase = (vm_offset_t)sc->console_buffer;
	sc->console.fb_video_dev = dev;
	sc->console.fb_name = device_get_nameunit(dev);
	sc->console.fb_flags = FB_FLAG_NOMMAP;
	error = hv_kms_attach(sc);
	if (error != 0)
		goto fail;
	error = vt_fb_attach(&sc->console);
	if (error != 0)
		goto fail;
	sc->console_attached = true;
	hv_bus_refresh_start(sc);
	device_printf(dev, "%ux%u, software rendering, DRM/KMS ready\n",
	    sc->width, sc->height);
	return (0);
fail:
	device_printf(dev, "attach failed: %d\n", error);
	hv_kms_cleanup(sc);
	if (sc->opened && sc->vram != NULL) {
		/* Return the host to the firmware console layout on failed attach. */
		sc->width = efi->fb_width;
		sc->height = efi->fb_height;
		sc->pitch = efi->fb_stride * 4;
		hv_host_situation(sc);
	}
	hv_bus_stop(sc);
	if (sc->vram != NULL)
		pmap_unmapdev(sc->vram, sc->vram_size);
	free(sc->console_buffer, M_DEVBUF);
	return (error);
}

static int
hv_drm_detach(device_t dev)
{
	/* Keep GEM mappings, VT, and callback code valid until reboot. */
	device_printf(dev, "attached display cannot be unloaded; reboot to remove\n");
	return (EBUSY);
}

static int
hv_drm_suspend(device_t dev)
{
	device_printf(dev, "guest suspend is not supported by this driver yet\n");
	return (EBUSY);
}

static device_method_t hv_drm_methods[] = {
	DEVMETHOD(device_probe, hv_drm_probe),
	DEVMETHOD(device_attach, hv_drm_attach),
	DEVMETHOD(device_detach, hv_drm_detach),
	DEVMETHOD(device_suspend, hv_drm_suspend),
	DEVMETHOD(fb_getinfo, hv_drm_getinfo),
	DEVMETHOD_END
};
static driver_t hv_drm_driver = {
	"hypervdrm", hv_drm_methods, sizeof(struct hv_drm_softc)
};

/* Deliberately no PNP autoload: first loading must be an explicit action. */
DRIVER_MODULE(hypervdrm, vmbus, hv_drm_driver, NULL, NULL);
MODULE_VERSION(hypervdrm, 1);
MODULE_DEPEND(hypervdrm, vmbus, 1, 1, 1);
MODULE_DEPEND(hypervdrm, drmn, 2, 2, 2);
MODULE_DEPEND(hypervdrm, linuxkpi, 1, 1, 1);
MODULE_DEPEND(hypervdrm, linuxkpi_video, 1, 1, 1);
MODULE_DEPEND(hypervdrm, fbd, 1, 1, 1);

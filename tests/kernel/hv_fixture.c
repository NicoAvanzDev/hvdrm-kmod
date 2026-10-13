/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the real KMS code on a non-Hyper-V test guest. No VT takeover. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include "hv_drm.h"
#include "bus_if.h"

int
hv_host_dirty(struct hv_drm_softc *sc, int x1, int y1, int x2, int y2)
{
	return (0);
}

int
hv_host_situation(struct hv_drm_softc *sc)
{
	return (0);
}

static int
fixture_probe(device_t dev)
{
	device_set_desc(dev, "hypervdrm KMS test fixture (RAM only)");
	return (BUS_PROBE_SPECIFIC);
}

static int
fixture_attach(device_t dev)
{
	struct hv_drm_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->width = 1920;
	sc->height = 1080;
	sc->pitch = sc->width * 4;
	sc->vram_size = 8 * 1024 * 1024;
	sc->vram = malloc(sc->vram_size, M_DEVBUF, M_WAITOK | M_ZERO);
	error = hv_kms_attach(sc);
	if (error != 0) {
		hv_kms_cleanup(sc);
		free(sc->vram, M_DEVBUF);
		return (error);
	}
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "pixel",
	    CTLFLAG_RD, sc->vram, 0, "First scanout pixel");
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "last_pixel",
	    CTLFLAG_RD, (uint32_t *)((uint8_t *)sc->vram +
	    (sc->height - 1) * sc->pitch + (sc->width - 1) * 4), 0,
	    "Last visible scanout pixel");
	return (0);
}

static int
fixture_detach(device_t dev)
{
	return (EBUSY);
}

static int
fixture_event(module_t mod, int event, void *argument)
{
	device_t nexus;
	if (event == MOD_LOAD) {
		nexus = devclass_get_device(devclass_find("nexus"), 0);
		if (nexus == NULL || BUS_ADD_CHILD(nexus, 0, "hypervdrm_fixture", 0) == NULL)
			return (ENXIO);
	}
	return (0);
}

static device_method_t fixture_methods[] = {
	DEVMETHOD(device_probe, fixture_probe),
	DEVMETHOD(device_attach, fixture_attach),
	DEVMETHOD(device_detach, fixture_detach),
	DEVMETHOD_END
};
static driver_t fixture_driver = {
	"hypervdrm_fixture", fixture_methods, sizeof(struct hv_drm_softc)
};
DRIVER_MODULE(hypervdrm_fixture, nexus, fixture_driver, fixture_event, NULL);
MODULE_DEPEND(hypervdrm_fixture, drmn, 2, 2, 2);
MODULE_DEPEND(hypervdrm_fixture, linuxkpi, 1, 1, 1);

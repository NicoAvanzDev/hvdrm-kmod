/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef HV_DRM_H
#define HV_DRM_H

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/fbio.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>

#include "hv_protocol.h"

struct drm_device;
struct device;
struct hv_kms;

#define HV_RING_SIZE (256 * 1024)

struct hv_drm_softc {
	device_t dev;
	struct vmbus_channel *channel;
	struct mtx lock;
	struct cv response_cv;
	struct callout refresh;
	struct task reconfigure;
	struct hv_message response;
	uint32_t expected, version;
	int response_ready;
	bool opened, ready, console_attached, registered;
	uint8_t *receive_buffer;
	struct fb_info console;
	struct drm_device *drm;
	struct device *linux_device;
	struct hv_kms *kms;
	uint32_t width, height, pitch;
	uint64_t vram_address, vram_size;
	void *vram;
};

int hv_bus_start(struct hv_drm_softc *);
void hv_bus_stop(struct hv_drm_softc *);
void hv_bus_refresh_start(struct hv_drm_softc *);
int hv_host_configure(struct hv_drm_softc *);
int hv_host_situation(struct hv_drm_softc *);
int hv_host_dirty(struct hv_drm_softc *, int, int, int, int);
int hv_kms_attach(struct hv_drm_softc *);
void hv_kms_cleanup(struct hv_drm_softc *);

#endif

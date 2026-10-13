/* SPDX-License-Identifier: BSD-2-Clause */
#include "hv_drm.h"
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <linux/device.h>
#include <linux/iosys-map.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>

struct hv_buffer {
	struct drm_gem_object gem;
	void *data;
};
struct hv_kms {
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

#ifdef HV_TRACE
#define HV_STEP(sc, text) device_printf((sc)->dev, "KMS: %s\n", text)
#else
#define HV_STEP(sc, text) do { } while (0)
#endif

static struct hv_drm_softc *
hv_sc(struct drm_device *dev)
{
	return (dev->dev_private);
}

static void
hv_buffer_free(struct drm_gem_object *gem)
{
	struct hv_buffer *buffer = container_of(gem, struct hv_buffer, gem);
	contigfree(buffer->data, gem->size, M_DEVBUF);
	drm_gem_object_release(gem);
	kfree(buffer);
}

static int
hv_buffer_vmap(struct drm_gem_object *gem, struct iosys_map *map)
{
	struct hv_buffer *buffer = container_of(gem, struct hv_buffer, gem);
	iosys_map_set_vaddr(map, buffer->data);
	return (0);
}

/* LinuxKPI's OBJT_DEVICE pager maps this contiguous RAM and holds a GEM ref. */
static const struct vm_operations_struct hv_buffer_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static int
hv_buffer_mmap(struct drm_gem_object *gem, struct vm_area_struct *vma)
{
	struct hv_buffer *buffer = container_of(gem, struct hv_buffer, gem);
	/* The native dma-buf shim lacks a VM mapping bridge; use the DRM fd. */
	if (vma->vm_mm == NULL)
		return (-EOPNOTSUPP);
	if (vma->vm_end - vma->vm_start > gem->size)
		return (-EINVAL);
	vma->vm_ops = &hv_buffer_vm_ops;
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = VM_MEMATTR_DEFAULT;
	vma->vm_pfn = pmap_kextract((vm_offset_t)buffer->data) >> PAGE_SHIFT;
	vma->vm_len = gem->size;
	return (0);
}

static struct sg_table *
hv_buffer_sg_table(struct drm_gem_object *gem)
{
	struct hv_buffer *buffer = container_of(gem, struct hv_buffer, gem);
	struct sg_table *table;
	int error;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (table == NULL)
		return (ERR_PTR(-ENOMEM));
	error = sg_alloc_table(table, 1, GFP_KERNEL);
	if (error != 0) {
		kfree(table);
		return (ERR_PTR(error));
	}
	/* Buffer RAM is physically contiguous and wired for the GEM lifetime. */
	sg_set_page(table->sgl, virt_to_page(buffer->data), gem->size, 0);
	return (table);
}

static const struct drm_gem_object_funcs hv_buffer_funcs = {
	.free = hv_buffer_free,
	.vmap = hv_buffer_vmap,
	.mmap = hv_buffer_mmap,
	.get_sg_table = hv_buffer_sg_table,
};

static int
hv_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct hv_drm_softc *sc = hv_sc(dev);
	struct hv_buffer *buffer;
	uint64_t pitch, size;
	int error;

	if (args->bpp != 32 || args->width == 0 || args->height == 0 ||
	    args->width > sc->width || args->height > sc->height)
		return (-EINVAL);
	pitch = roundup2((uint64_t)args->width * 4, 64);
	size = roundup2(pitch * args->height, PAGE_SIZE);
	if (pitch > UINT32_MAX || size > roundup2(sc->vram_size, PAGE_SIZE))
		return (-EINVAL);
	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (buffer == NULL)
		return (-ENOMEM);
	buffer->data = contigmalloc(size, M_DEVBUF, M_NOWAIT | M_ZERO,
	    0, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
	if (buffer->data == NULL) {
		kfree(buffer);
		return (-ENOMEM);
	}
	drm_gem_private_object_init(dev, &buffer->gem, size);
	buffer->gem.funcs = &hv_buffer_funcs;
	error = drm_gem_create_mmap_offset(&buffer->gem);
	if (error == 0)
		error = drm_gem_handle_create(file, &buffer->gem, &args->handle);
	if (error == 0) {
		args->pitch = pitch;
		args->size = size;
	}
	drm_gem_object_put(&buffer->gem);
	return (error);
}

DEFINE_DRM_GEM_FOPS(hv_file_ops);

/* Called with display_lock held; console writes only affect its RAM buffer. */
static void
hv_console_copy(struct hv_drm_softc *sc)
{
	mtx_assert(&sc->display_lock, MA_OWNED);
	if (!sc->display_owned && sc->console_buffer != NULL) {
		memcpy(sc->vram, sc->console_buffer, (size_t)sc->pitch * sc->height);
		hv_host_dirty(sc, 0, 0, sc->width, sc->height);
	}
}

void
hv_console_refresh(struct hv_drm_softc *sc)
{
	mtx_lock(&sc->display_lock);
	hv_console_copy(sc);
	mtx_unlock(&sc->display_lock);
}

/* Shutdown and panic stop the refresh callout, so show the console now. */
void
hv_console_show(struct hv_drm_softc *sc)
{
	/* After a panic the lock holder may be the panicking thread itself. */
	bool panicked = KERNEL_PANICKED();

	if (!panicked)
		mtx_lock(&sc->display_lock);
	sc->display_owned = false;
	memcpy(sc->vram, sc->console_buffer, (size_t)sc->pitch * sc->height);
	hv_host_dirty(sc, 0, 0, sc->width, sc->height);
	if (!panicked)
		mtx_unlock(&sc->display_lock);
}

static void
hv_master_set(struct drm_device *dev, struct drm_file *file, bool from_open)
{
	struct hv_drm_softc *sc = hv_sc(dev);
	mtx_lock(&sc->display_lock);
	sc->display_owned = true;
	/* Reacquiring after a VT switch must replace the whole console image. */
	sc->full_redraw = true;
	mtx_unlock(&sc->display_lock);
}

static void
hv_master_drop(struct drm_device *dev, struct drm_file *file)
{
	struct hv_drm_softc *sc = hv_sc(dev);
	mtx_lock(&sc->display_lock);
	sc->display_owned = false;
	hv_console_copy(sc);
	mtx_unlock(&sc->display_lock);
}

static const struct drm_driver hv_drm_ops = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.name = "hypervdrm",
	.desc = "FreeBSD Hyper-V software display",
	.date = "20261013",
	.major = 0, .minor = 2,
	.fops = &hv_file_ops,
	.master_set = hv_master_set,
	.master_drop = hv_master_drop,
	.dumb_create = hv_dumb_create,
	.dumb_map_offset = drm_gem_dumb_map_offset,
	/* Generic import accepts our own exports; cross-device import is absent. */
	.gem_prime_import = drm_gem_prime_import,
};

static int
hv_connector_modes(struct drm_connector *connector)
{
	struct hv_drm_softc *sc = hv_sc(connector->dev);
	struct drm_display_mode *mode;
	int delta;

	mode = drm_cvt_mode(connector->dev, sc->width, sc->height, 60, false, false, false);
	if (mode == NULL)
		return (0);
	/* CVT rounds widths to cells of eight; preserve the host's exact mode. */
	delta = sc->width - mode->hdisplay;
	mode->hdisplay += delta;
	mode->hsync_start += delta;
	mode->hsync_end += delta;
	mode->htotal += delta;
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	return (1);
}

static enum drm_connector_status
hv_connector_detect(struct drm_connector *connector, bool force)
{
	return (connector_status_connected);
}

static const struct drm_connector_funcs hv_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = hv_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};
static const struct drm_connector_helper_funcs hv_connector_helpers = {
	.get_modes = hv_connector_modes,
};

static enum drm_mode_status
hv_mode_valid(struct drm_device *dev, const struct drm_display_mode *mode)
{
	struct hv_drm_softc *sc = hv_sc(dev);
	return (mode->hdisplay == sc->width && mode->vdisplay == sc->height &&
	    !(mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)) ?
	    MODE_OK : MODE_BAD);
}

static const struct drm_framebuffer_funcs hv_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
	.dirty = drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *
hv_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	struct hv_drm_softc *sc = hv_sc(dev);
	struct drm_gem_object *gem;
	struct drm_framebuffer *fb;
	int error;

	if (cmd->pixel_format != DRM_FORMAT_XRGB8888 || cmd->width != sc->width ||
	    cmd->height != sc->height || cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR)
		return (ERR_PTR(-EINVAL));
	gem = drm_gem_object_lookup(file, cmd->handles[0]);
	if (gem == NULL)
		return (ERR_PTR(-ENOENT));
	if (gem->funcs != &hv_buffer_funcs || cmd->offsets[0] > gem->size ||
	    !hv_surface_fits(cmd->width, cmd->height, cmd->pitches[0],
	    gem->size - cmd->offsets[0])) {
		drm_gem_object_put(gem);
		return (ERR_PTR(-EINVAL));
	}
	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL) {
		drm_gem_object_put(gem);
		return (ERR_PTR(-ENOMEM));
	}
	drm_helper_mode_fill_fb_struct(dev, fb, cmd);
	fb->obj[0] = gem;
	error = drm_framebuffer_init(dev, fb, &hv_fb_funcs);
	if (error != 0) {
		drm_gem_object_put(gem);
		kfree(fb);
		return (ERR_PTR(error));
	}
	return (fb);
}

static const struct drm_mode_config_funcs hv_mode_funcs = {
	.fb_create = hv_fb_create,
	.mode_valid = hv_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int
hv_plane_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *p = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc;
	if (p->crtc == NULL)
		return (0);
	crtc = drm_atomic_get_new_crtc_state(state, p->crtc);
	if (p->src_x != 0 || p->src_y != 0)
		return (-EINVAL);
	return (drm_atomic_helper_check_plane_state(p, crtc, DRM_PLANE_NO_SCALING,
	    DRM_PLANE_NO_SCALING, false, false));
}

static void
hv_plane_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct hv_drm_softc *sc = hv_sc(plane->dev);
	struct drm_plane_state *p = drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old = drm_atomic_get_old_plane_state(state, plane);
	struct hv_buffer *buffer;
	struct drm_rect damage;
	uint8_t *source, *target;
	int y;

	if (p->fb == NULL || !p->visible)
		return;
	buffer = container_of(drm_gem_fb_get_obj(p->fb, 0), struct hv_buffer, gem);
	mtx_lock(&sc->display_lock);
	if (!sc->display_owned) {
		mtx_unlock(&sc->display_lock);
		return;
	}
	if (sc->full_redraw) {
		damage = (struct drm_rect){ 0, 0, sc->width, sc->height };
	} else if (!drm_atomic_helper_damage_merged(old, p, &damage)) {
		mtx_unlock(&sc->display_lock);
		return;
	}
	source = (uint8_t *)buffer->data + p->fb->offsets[0];
	target = sc->vram;
	for (y = damage.y1; y < damage.y2; y++)
		memcpy(target + (size_t)y * sc->pitch + damage.x1 * 4,
		    source + (size_t)y * p->fb->pitches[0] + damage.x1 * 4,
		    (damage.x2 - damage.x1) * 4);
	hv_host_dirty(sc, damage.x1, damage.y1, damage.x2, damage.y2);
	sc->full_redraw = false;
	mtx_unlock(&sc->display_lock);
}

static const struct drm_plane_funcs hv_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};
static const struct drm_plane_helper_funcs hv_plane_helpers = {
	.prepare_fb = drm_gem_plane_helper_prepare_fb,
	.atomic_check = hv_plane_check,
	.atomic_update = hv_plane_update,
};
static const struct drm_crtc_funcs hv_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static void
hv_crtc_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	hv_host_situation(hv_sc(crtc->dev));
}

static const struct drm_crtc_helper_funcs hv_crtc_helpers = {
	.atomic_enable = hv_crtc_enable,
};
static const struct drm_encoder_funcs hv_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int
hv_kms_attach(struct hv_drm_softc *sc)
{
	static const uint32_t formats[] = { DRM_FORMAT_XRGB8888 };
	static const uint64_t modifiers[] = { DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID };
	struct hv_kms *kms;
	struct device *device;
	struct drm_device *drm;
	int error;

	mtx_init(&sc->display_lock, "hvdisplay", NULL, MTX_DEF);
	sc->display_lock_initialized = true;
	sc->full_redraw = true;
	kms = kzalloc(sizeof(*kms), GFP_KERNEL);
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (kms == NULL || device == NULL) {
		kfree(kms);
		kfree(device);
		return (ENOMEM);
	}
	sc->kms = kms;
	sc->linux_device = device;
	HV_STEP(sc, "initialize LinuxKPI device");
	/* Preserve the native newbus softc instead of device_initialize(). */
	device->bsddev = sc->dev;
	device->parent = &linux_root_device;
	device->release = device_create_release;
	INIT_LIST_HEAD(&device->irqents);
	INIT_LIST_HEAD(&device->devres_head);
	spin_lock_init(&device->devres_lock);
	kobject_init(&device->kobj, &linux_dev_ktype);
	error = dev_set_name(device, "%s", device_get_nameunit(sc->dev));
	if (error != 0)
		return (-error);
	HV_STEP(sc, "allocate DRM device");
	drm = drm_dev_alloc(&hv_drm_ops, device);
	if (IS_ERR(drm))
		return (-PTR_ERR(drm));
	sc->drm = drm;
	drm->dev_private = sc;
	HV_STEP(sc, "initialize mode configuration");
	error = drmm_mode_config_init(drm);
	if (error != 0)
		return (-error);
	drm->mode_config.min_width = drm->mode_config.max_width = sc->width;
	drm->mode_config.min_height = drm->mode_config.max_height = sc->height;
	drm->mode_config.preferred_depth = 32;
	drm->mode_config.prefer_shadow = 1;
	drm->mode_config.funcs = &hv_mode_funcs;
	HV_STEP(sc, "initialize display objects");
	error = drm_universal_plane_init(drm, &kms->plane, 0, &hv_plane_funcs,
	    formats, nitems(formats), modifiers, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (error != 0)
		return (-error);
	drm_plane_helper_add(&kms->plane, &hv_plane_helpers);
	drm_plane_enable_fb_damage_clips(&kms->plane);
	error = drm_crtc_init_with_planes(drm, &kms->crtc, &kms->plane, NULL, &hv_crtc_funcs, NULL);
	if (error != 0)
		return (-error);
	drm_crtc_helper_add(&kms->crtc, &hv_crtc_helpers);
	error = drm_encoder_init(drm, &kms->encoder, &hv_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (error != 0)
		return (-error);
	kms->encoder.possible_crtcs = drm_crtc_mask(&kms->crtc);
	error = drm_connector_init(drm, &kms->connector, &hv_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	if (error != 0)
		return (-error);
	drm_connector_helper_add(&kms->connector, &hv_connector_helpers);
	error = drm_connector_attach_encoder(&kms->connector, &kms->encoder);
	if (error != 0)
		return (-error);
	drm_mode_config_reset(drm);
	HV_STEP(sc, "register DRM device");
	/* No vblank IRQ: atomic helpers emit completion events after each blit. */
	error = drm_dev_register(drm, 0);
	if (error != 0)
		return (-error);
	sc->registered = true;
	snprintf(sc->drm_device, sizeof(sc->drm_device), "/dev/dri/card%d",
	    drm->primary->index);
	SYSCTL_ADD_STRING(device_get_sysctl_ctx(sc->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)), OID_AUTO,
	    "drm_device", CTLFLAG_RD, sc->drm_device, 0, "DRM primary device node");
	HV_STEP(sc, "registration complete");
	return (0);
}

void
hv_kms_cleanup(struct hv_drm_softc *sc)
{
	if (sc->drm != NULL) {
		if (sc->registered)
			drm_dev_unregister(sc->drm);
		drm_dev_put(sc->drm);
		sc->drm = NULL;
	}
	if (sc->linux_device != NULL) {
		put_device(sc->linux_device);
		sc->linux_device = NULL;
	}
	kfree(sc->kms);
	sc->kms = NULL;
	if (sc->display_lock_initialized) {
		mtx_destroy(&sc->display_lock);
		sc->display_lock_initialized = false;
	}
}

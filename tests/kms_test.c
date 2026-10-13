/* SPDX-License-Identifier: BSD-2-Clause */
/* Run as root only in a disposable FreeBSD guest with the RAM fixture. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

struct buffer {
	struct drm_mode_create_dumb dumb;
	uint32_t fb;
	uint32_t *pixels;
};

static void
create_buffer(int fd, const char *path, struct buffer *b, uint32_t width, uint32_t height, uint32_t pixel)
{
	struct drm_mode_map_dumb map = { 0 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	uint32_t imported;
	uint32_t *shared;
	struct drm_gem_close close_import = { 0 };
	int prime_fd, receiver;
	size_t i;

	memset(b, 0, sizeof(*b));
	b->dumb.width = width;
	b->dumb.height = height;
	b->dumb.bpp = 32;
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &b->dumb) != 0)
		err(1, "CREATE_DUMB");
	map.handle = b->dumb.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0)
		err(1, "MAP_DUMB");
	b->pixels = mmap(NULL, b->dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (b->pixels == MAP_FAILED)
		err(1, "mmap");
	for (i = 0; i < b->dumb.size / sizeof(uint32_t); i++)
		b->pixels[i] = pixel;
	assert(drmPrimeHandleToFD(fd, b->dumb.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd) == 0);
	assert(drmPrimeFDToHandle(fd, prime_fd, &imported) == 0);
	assert(imported == b->dumb.handle);
	receiver = open(path, O_RDWR);
	assert(receiver >= 0);
	assert(drmPrimeFDToHandle(receiver, prime_fd, &close_import.handle) == 0);
	assert(ioctl(receiver, DRM_IOCTL_GEM_CLOSE, &close_import) == 0);
	close(receiver);
	shared = mmap(NULL, b->dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, prime_fd, 0);
	assert(shared == MAP_FAILED && errno == EOPNOTSUPP);
	close(prime_fd);
	handles[0] = b->dumb.handle;
	pitches[0] = b->dumb.pitch;
	if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888,
	    handles, pitches, offsets, &b->fb, 0) != 0)
		err(1, "ADDFB2");
	offsets[0] = UINT32_MAX;
	assert(drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888,
	    handles, pitches, offsets, &handles[3], 0) != 0);
}

static void
check_pixel(uint32_t wanted)
{
	uint32_t pixel;
	size_t length = sizeof(pixel);
	if (sysctlbyname("dev.hypervdrm_fixture.0.pixel", &pixel, &length, NULL, 0) != 0)
		err(1, "fixture pixel");
	assert(pixel == wanted);
	length = sizeof(pixel);
	if (sysctlbyname("dev.hypervdrm_fixture.0.last_pixel", &pixel, &length, NULL, 0) != 0)
		err(1, "fixture last pixel");
	assert(pixel == wanted);
}

static void
flip_complete(int fd, unsigned int sequence, unsigned int seconds,
    unsigned int microseconds, void *context)
{
	(*(int *)context)++;
}

static void
wait_flip(int fd, int *done)
{
	struct pollfd pollfd = { fd, POLLIN, 0 };
	drmEventContext events = { .version = 2, .page_flip_handler = flip_complete };
	assert(poll(&pollfd, 1, 2000) == 1);
	assert(drmHandleEvent(fd, &events) == 0);
	assert(*done == 1);
}

static uint32_t
property(int fd, uint32_t object, uint32_t object_type, const char *name)
{
	drmModeObjectProperties *properties = drmModeObjectGetProperties(fd, object, object_type);
	drmModePropertyRes *p;
	uint32_t i, id = 0;
	assert(properties != NULL);
	for (i = 0; i < properties->count_props; i++) {
		p = drmModeGetProperty(fd, properties->props[i]);
		assert(p != NULL);
		if (strcmp(p->name, name) == 0)
			id = p->prop_id;
		drmModeFreeProperty(p);
	}
	drmModeFreeObjectProperties(properties);
	assert(id != 0);
	return (id);
}

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/dri/card0";
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModePlaneRes *planes;
	drmModeAtomicReq *atomic;
	drmVersion *version;
	struct buffer a, b;
	struct drm_mode_destroy_dumb destroy;
	struct drm_mode_create_dumb bad = { .width = 1920, .height = 1080, .bpp = 24 };
	uint32_t crtc, fb_property;
	int fd, done = 0;

	fd = open(path, O_RDWR);
	if (fd < 0)
		err(1, "open %s", path);
	version = drmGetVersion(fd);
	assert(version != NULL && strcmp(version->name, "hypervdrm") == 0);
	drmFreeVersion(version);
	assert(drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);
	assert(ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &bad) != 0);
	resources = drmModeGetResources(fd);
	assert(resources != NULL && resources->count_crtcs == 1 && resources->count_connectors == 1);
	crtc = resources->crtcs[0];
	connector = drmModeGetConnector(fd, resources->connectors[0]);
	assert(connector != NULL && connector->connection == DRM_MODE_CONNECTED && connector->count_modes == 1);
	create_buffer(fd, path, &a, connector->modes[0].hdisplay, connector->modes[0].vdisplay, 0x00123456);
	create_buffer(fd, path, &b, connector->modes[0].hdisplay, connector->modes[0].vdisplay, 0x00654321);
	assert(drmModeSetCrtc(fd, crtc, a.fb, 0, 0, &connector->connector_id, 1, &connector->modes[0]) == 0);
	check_pixel(0x00123456);
	assert(drmModePageFlip(fd, crtc, b.fb, DRM_MODE_PAGE_FLIP_EVENT, &done) == 0);
	wait_flip(fd, &done);
	check_pixel(0x00654321);

	planes = drmModeGetPlaneResources(fd);
	assert(planes != NULL && planes->count_planes == 1);
	fb_property = property(fd, planes->planes[0], DRM_MODE_OBJECT_PLANE, "FB_ID");
	atomic = drmModeAtomicAlloc();
	assert(atomic != NULL);
	assert(drmModeAtomicAddProperty(atomic, planes->planes[0], fb_property, a.fb) >= 0);
	assert(drmModeAtomicCommit(fd, atomic, DRM_MODE_ATOMIC_TEST_ONLY, NULL) == 0);
	check_pixel(0x00654321);
	done = 0;
	assert(drmModeAtomicCommit(fd, atomic, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &done) == 0);
	wait_flip(fd, &done);
	check_pixel(0x00123456);
	drmModeAtomicFree(atomic);
	drmModeFreePlaneResources(planes);
	assert(drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL) == 0);
	assert(drmModeRmFB(fd, a.fb) == 0);
	assert(drmModeRmFB(fd, b.fb) == 0);
	assert(munmap(a.pixels, a.dumb.size) == 0);
	assert(munmap(b.pixels, b.dumb.size) == 0);
	destroy.handle = a.dumb.handle;
	assert(ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) == 0);
	destroy.handle = b.dumb.handle;
	assert(ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) == 0);
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
	close(fd);
	puts("GEM mmap, PRIME export/import, bounds, modeset, page flips, and atomic commits: passed (RAM fixture)");
	return (0);
}

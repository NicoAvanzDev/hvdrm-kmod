/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2017 Microsoft Corp.
 *
 * Derived from the SynthVID transport in FreeBSD review D39395, diff 119805.
 * See LICENSE for redistribution conditions and disclaimer.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include "hv_drm.h"

static int
hv_send(struct hv_drm_softc *sc, struct hv_message *message, size_t length)
{
	message->pipe_type = HV_PIPE_DATA;
	message->pipe_size = message->size = length - 8;
	return (vmbus_chan_send(sc->channel, VMBUS_CHANPKT_TYPE_INBAND,
	    VMBUS_CHANPKT_FLAG_NONE, message, length, 0));
}

/* Requests run only during attach; callbacks never wait for host replies. */
static int
hv_request(struct hv_drm_softc *sc, struct hv_message *message, size_t length,
    uint32_t expected, struct hv_message *response)
{
	int error;

	mtx_lock(&sc->lock);
	sc->expected = expected;
	sc->response_ready = 0;
	error = hv_send(sc, message, length);
	if (error == 0) {
		while (!sc->response_ready) {
			error = cv_timedwait(&sc->response_cv, &sc->lock, 10 * hz);
			if (error != 0)
				break;
		}
	}
	if (error == 0)
		*response = sc->response;
	sc->expected = 0;
	mtx_unlock(&sc->lock);
	return (error == EWOULDBLOCK ? ETIMEDOUT : error);
}

int
hv_host_situation(struct hv_drm_softc *sc)
{
	struct hv_message m = { 0 };

	m.type = HV_SITUATION;
	m.situation.outputs = 1;
	m.situation.active = 1;
	m.situation.depth = 32;
	m.situation.width = sc->width;
	m.situation.height = sc->height;
	m.situation.pitch = sc->pitch;
	return (hv_send(sc, &m, HV_MESSAGE_SIZE(situation)));
}

static int
hv_hide_pointer(struct hv_drm_softc *sc)
{
	struct hv_message m = { 0 };
	int error;

	m.type = HV_POINTER_POSITION;
	m.pointer_position.visible = 1;
	error = hv_send(sc, &m, HV_MESSAGE_SIZE(pointer_position));
	if (error != 0)
		return (error);
	memset(&m, 0, sizeof(m));
	m.type = HV_POINTER_SHAPE;
	m.pointer_shape.part = 0xff;
	m.pointer_shape.argb = 1;
	m.pointer_shape.width = m.pointer_shape.height = 1;
	/* A transparent pixel hides the host cursor; the compositor draws it. */
	return (hv_send(sc, &m, HV_MESSAGE_SIZE(pointer_shape)));
}

int
hv_host_dirty(struct hv_drm_softc *sc, int x1, int y1, int x2, int y2)
{
	struct hv_message m = { 0 };

	if (x1 < 0 || y1 < 0 || x2 > (int)sc->width ||
	    y2 > (int)sc->height || x1 >= x2 || y1 >= y2)
		return (EINVAL);
	m.type = HV_DIRTY;
	m.dirty.count = 1;
	m.dirty.rect = (struct hv_rect){ x1, y1, x2, y2 };
	/* Flush framebuffer stores before notifying the host. */
	atomic_thread_fence_seq_cst();
	return (hv_send(sc, &m, HV_MESSAGE_SIZE(dirty)));
}

static void
hv_reconfigure(void *context, int pending __unused)
{
	struct hv_drm_softc *sc = context;

	mtx_lock(&sc->lock);
	if (sc->ready) {
		hv_hide_pointer(sc);
		hv_host_situation(sc);
		hv_console_refresh(sc);
		hv_host_dirty(sc, 0, 0, sc->width, sc->height);
	}
	mtx_unlock(&sc->lock);
}

static void
hv_refresh(void *context)
{
	struct hv_drm_softc *sc = context;

	mtx_assert(&sc->lock, MA_OWNED);
	if (sc->ready) {
		/* VT draws in private RAM; copy it only while no DRM master owns VRAM. */
		hv_console_refresh(sc);
		callout_schedule(&sc->refresh, MAX(1, hz / 30));
	}
}

static void
hv_receive(struct vmbus_channel *channel, void *context)
{
	struct hv_drm_softc *sc = context;
	struct vmbus_chanpkt_hdr *packet;
	const struct hv_message *m;
	uint32_t type;
	int error, length, header_length, total_length;

	for (;;) {
		packet = (void *)sc->receive_buffer;
		length = HV_RING_SIZE;
		error = vmbus_chan_recv_pkt(channel, packet, &length);
		if (error == EAGAIN)
			return;
		if (error != 0) {
			device_printf(sc->dev, "receive error: %d\n", error);
			return;
		}
		header_length = VMBUS_CHANPKT_GETLEN(packet->cph_hlen);
		total_length = VMBUS_CHANPKT_GETLEN(packet->cph_tlen);
		if (packet->cph_type != VMBUS_CHANPKT_TYPE_INBAND ||
		    header_length < (int)sizeof(*packet) || total_length > length ||
		    header_length > total_length)
			continue;
		m = (const void *)(sc->receive_buffer + header_length);
		if (!hv_message_validate(m, total_length - header_length, &type))
			continue;
		mtx_lock(&sc->lock);
		if (type == sc->expected && !sc->response_ready) {
			memset(&sc->response, 0, sizeof(sc->response));
			memcpy(&sc->response, m, MIN(sizeof(sc->response), m->size + 8));
			sc->response_ready = 1;
			cv_signal(&sc->response_cv);
		} else if (type == HV_FEATURE_CHANGE && sc->ready) {
			taskqueue_enqueue(taskqueue_thread, &sc->reconfigure);
		}
		mtx_unlock(&sc->lock);
	}
}

int
hv_bus_start(struct hv_drm_softc *sc)
{
	struct hv_message request = { 0 }, response;
	const uint32_t versions[] = { HV_VERSION_35, HV_VERSION_32 };
	unsigned int i;
	int error;

	mtx_init(&sc->lock, "hypervdrm", NULL, MTX_DEF);
	cv_init(&sc->response_cv, "hvvideo");
	callout_init_mtx(&sc->refresh, &sc->lock, 0);
	TASK_INIT(&sc->reconfigure, 0, hv_reconfigure, sc);
	sc->receive_buffer = malloc(HV_RING_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->channel = vmbus_get_channel(sc->dev);
	vmbus_chan_set_readbatch(sc->channel, false);
	error = vmbus_chan_open(sc->channel, HV_RING_SIZE, HV_RING_SIZE,
	    NULL, 0, hv_receive, sc);
	if (error != 0)
		return (error);
	sc->opened = true;
	for (i = 0; i < nitems(versions); i++) {
		request.type = HV_VERSION_REQUEST;
		request.version_request.version = versions[i];
		error = hv_request(sc, &request, HV_MESSAGE_SIZE(version_request),
		    HV_VERSION_RESPONSE, &response);
		if (error != 0)
			return (error);
		if (response.version_response.accepted) {
			sc->version = versions[i];
			break;
		}
	}
	if (sc->version == 0)
		return (EPROTONOSUPPORT);
	device_printf(sc->dev, "SynthVID %u.%u\n", sc->version & 0xffff,
	    sc->version >> 16);
	if (sc->version == HV_VERSION_35) {
		memset(&request, 0, sizeof(request));
		request.type = HV_RESOLUTION_REQUEST;
		request.resolution_request.count = HV_MAX_MODES;
		error = hv_request(sc, &request, HV_MESSAGE_SIZE(resolution_request),
		    HV_RESOLUTION_RESPONSE, &response);
		if (error != 0)
			return (error);
		i = response.resolution_response.preferred;
		sc->width = response.resolution_response.modes[i].width;
		sc->height = response.resolution_response.modes[i].height;
	}
	return (0);
}

int
hv_host_configure(struct hv_drm_softc *sc)
{
	struct hv_message request = { 0 }, response;
	int error;

	request.type = HV_VRAM_LOCATION;
	request.vram.context = request.vram.address = sc->vram_address;
	request.vram.specified = 1;
	error = hv_request(sc, &request, HV_MESSAGE_SIZE(vram), HV_VRAM_ACK, &response);
	if (error != 0)
		return (error);
	if (response.vram_ack.context != sc->vram_address)
		return (EPROTO);
	error = hv_hide_pointer(sc);
	if (error == 0)
		error = hv_host_situation(sc);
	return (error);
}

void
hv_bus_stop(struct hv_drm_softc *sc)
{
	mtx_lock(&sc->lock);
	sc->ready = false;
	mtx_unlock(&sc->lock);
	callout_drain(&sc->refresh);
	if (sc->opened) {
		vmbus_chan_close(sc->channel);
		sc->opened = false;
	}
	taskqueue_drain(taskqueue_thread, &sc->reconfigure);
	free(sc->receive_buffer, M_DEVBUF);
	cv_destroy(&sc->response_cv);
	mtx_destroy(&sc->lock);
}

void
hv_bus_refresh_start(struct hv_drm_softc *sc)
{
	mtx_lock(&sc->lock);
	sc->ready = true;
	callout_reset(&sc->refresh, MAX(1, hz / 30), hv_refresh, sc);
	mtx_unlock(&sc->lock);
}

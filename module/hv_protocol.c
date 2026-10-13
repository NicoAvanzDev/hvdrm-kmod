/* SPDX-License-Identifier: BSD-2-Clause */
#include "hv_protocol.h"

_Static_assert(HV_PAYLOAD_OFFSET == 16, "wire header");
_Static_assert(sizeof(((struct hv_message *)0)->situation) == 27, "wire situation");
_Static_assert(sizeof(((struct hv_message *)0)->vram) == 17, "wire VRAM");
_Static_assert(sizeof(((struct hv_message *)0)->dirty) == 18, "wire dirty rectangle");

int
hv_message_validate(const void *data, size_t length, uint32_t *type)
{
	const struct hv_message *m = data;
	size_t minimum;

	if (length < HV_PAYLOAD_OFFSET || m->pipe_type != HV_PIPE_DATA ||
	    m->pipe_size < 8 || m->size != m->pipe_size ||
	    (uint64_t)m->pipe_size + 8 > length)
		return (0);
	switch (m->type) {
	case HV_VERSION_RESPONSE:
		minimum = HV_MESSAGE_SIZE(version_response);
		break;
	case HV_VRAM_ACK:
	case HV_SITUATION_ACK:
		minimum = HV_MESSAGE_SIZE(vram_ack);
		break;
	case HV_FEATURE_CHANGE:
		minimum = HV_MESSAGE_SIZE(feature);
		break;
	case HV_RESOLUTION_RESPONSE:
		minimum = HV_PAYLOAD_OFFSET + 131;
		if ((uint64_t)m->size + 8 < minimum)
			return (0);
		if (m->resolution_response.count == 0 ||
		    m->resolution_response.count > HV_MAX_MODES ||
		    m->resolution_response.preferred >= m->resolution_response.count)
			return (0);
		minimum += m->resolution_response.count * sizeof(struct hv_mode);
		break;
	default:
		return (0);
	}
	if ((uint64_t)m->size + 8 < minimum)
		return (0);
	*type = m->type;
	return (1);
}

int
hv_surface_fits(uint32_t width, uint32_t height, uint32_t pitch, uint64_t size)
{
	return (width != 0 && height != 0 && (uint64_t)width * 4 <= pitch &&
	    (uint64_t)pitch * height <= size);
}

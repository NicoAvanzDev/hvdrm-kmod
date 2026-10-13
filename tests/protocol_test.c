/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hv_protocol.h"

static void
header(struct hv_message *m, uint32_t type, size_t length)
{
	memset(m, 0, sizeof(*m));
	m->pipe_type = HV_PIPE_DATA;
	m->pipe_size = m->size = length - 8;
	m->type = type;
}

int
main(void)
{
	struct hv_message m;
	uint32_t type, random = 0x719cab;
	size_t i, j, length;

	header(&m, HV_VERSION_RESPONSE, HV_MESSAGE_SIZE(version_response));
	assert(hv_message_validate(&m, HV_MESSAGE_SIZE(version_response), &type));
	assert(type == HV_VERSION_RESPONSE);
	for (i = 0; i < HV_MESSAGE_SIZE(version_response); i++)
		assert(!hv_message_validate(&m, i, &type));
	m.size--;
	assert(!hv_message_validate(&m, sizeof(m), &type));
	m.size = m.pipe_size = UINT32_MAX;
	assert(!hv_message_validate(&m, sizeof(m), &type));

	length = HV_PAYLOAD_OFFSET + 131 + sizeof(struct hv_mode);
	header(&m, HV_RESOLUTION_RESPONSE, length);
	m.resolution_response.count = 1;
	m.resolution_response.modes[0] = (struct hv_mode){ 1920, 1080 };
	assert(hv_message_validate(&m, length, &type));
	m.resolution_response.preferred = 1;
	assert(!hv_message_validate(&m, length, &type));
	m.resolution_response.preferred = 0;
	m.resolution_response.count = 65;
	assert(!hv_message_validate(&m, sizeof(m), &type));
	m.resolution_response.count = 64;
	m.size = m.pipe_size = HV_MESSAGE_SIZE(resolution_response) - 8;
	assert(hv_message_validate(&m, sizeof(m), &type));
	assert(!hv_message_validate(&m, sizeof(m) - 1, &type));
	m.resolution_response.count = 0;
	assert(!hv_message_validate(&m, sizeof(m), &type));

	header(&m, HV_FEATURE_CHANGE, HV_MESSAGE_SIZE(feature));
	assert(hv_message_validate(&m, HV_MESSAGE_SIZE(feature), &type));
	m.type = 0;
	assert(!hv_message_validate(&m, sizeof(m), &type));

	assert(hv_surface_fits(1920, 1080, 7680, 8 * 1024 * 1024));
	assert(!hv_surface_fits(3840, 2160, 15360, 8 * 1024 * 1024));
	assert(!hv_surface_fits(1920, 1080, 7679, UINT64_MAX));
	assert(!hv_surface_fits(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT64_MAX));
	assert(!hv_surface_fits(0, 1, 4, 4));
	assert(!hv_surface_fits(1, 0, 4, 4));

	/* Exercise truncated and hostile host data under ASan/UBSan. */
	for (i = 0; i < 20000; i++) {
		for (j = 0; j < sizeof(m); j++) {
			random ^= random << 13;
			random ^= random >> 17;
			random ^= random << 5;
			((unsigned char *)&m)[j] = random;
		}
		(void)hv_message_validate(&m, i % (sizeof(m) + 1), &type);
	}
	puts("protocol validation and framebuffer bounds: passed");
	return (0);
}

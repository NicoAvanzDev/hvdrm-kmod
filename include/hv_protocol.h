/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef HV_PROTOCOL_H
#define HV_PROTOCOL_H

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/stddef.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define HV_PACKED __attribute__((__packed__))
#define HV_VERSION(major, minor) (((uint32_t)(minor) << 16) | (major))
#define HV_VERSION_35 HV_VERSION(3, 5)
#define HV_VERSION_32 HV_VERSION(3, 2)
#define HV_MAX_MODES 64
#define HV_PIPE_DATA 1

enum hv_message_type {
	HV_VERSION_REQUEST = 1, HV_VERSION_RESPONSE = 2,
	HV_VRAM_LOCATION = 3, HV_VRAM_ACK = 4,
	HV_SITUATION = 5, HV_SITUATION_ACK = 6,
	HV_POINTER_POSITION = 7, HV_POINTER_SHAPE = 8,
	HV_FEATURE_CHANGE = 9, HV_DIRTY = 10,
	HV_RESOLUTION_REQUEST = 13, HV_RESOLUTION_RESPONSE = 14
};

struct hv_mode { uint16_t width, height; } HV_PACKED;
struct hv_rect { int32_t x1, y1, x2, y2; } HV_PACKED;
struct hv_message {
	uint32_t pipe_type, pipe_size;
	uint32_t type, size;
	union {
		struct { uint32_t version; } HV_PACKED version_request;
		struct { uint32_t version; uint8_t accepted, outputs; } HV_PACKED version_response;
		struct { uint64_t context; uint8_t specified; uint64_t address; } HV_PACKED vram;
		struct { uint64_t context; } HV_PACKED vram_ack;
		struct {
			uint64_t context;
			uint8_t outputs, active;
			uint32_t offset;
			uint8_t depth;
			uint32_t width, height, pitch;
		} HV_PACKED situation;
		struct { uint8_t visible, output; int32_t x, y; } HV_PACKED pointer_position;
		struct {
			uint8_t part, argb;
			uint32_t width, height, hot_x, hot_y;
			uint8_t pixel[4];
		} HV_PACKED pointer_shape;
		struct { uint8_t dirty, position, shape, situation; } HV_PACKED feature;
		struct { uint8_t output, count; struct hv_rect rect; } HV_PACKED dirty;
		struct { uint8_t count; } HV_PACKED resolution_request;
		struct {
			uint8_t edid[128], count, preferred, standard;
			struct hv_mode modes[HV_MAX_MODES];
		} HV_PACKED resolution_response;
	};
} HV_PACKED;

#define HV_PAYLOAD_OFFSET offsetof(struct hv_message, version_request)
#define HV_MESSAGE_SIZE(member) (HV_PAYLOAD_OFFSET + sizeof(((struct hv_message *)0)->member))

/* This driver targets little-endian amd64, as does the host wire format. */
int hv_message_validate(const void *, size_t, uint32_t *);
int hv_surface_fits(uint32_t, uint32_t, uint32_t, uint64_t);

#endif

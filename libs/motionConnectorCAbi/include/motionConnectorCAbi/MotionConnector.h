/* SPDX-License-Identifier: Apache-2.0
 *
 * The in-process acquisition C ABI (docs/design/C_ABI.md). It ends where a
 * connector ends, at observations and the shared pose: no filter, solve,
 * retarget, recording, USD or runtime loop crosses it. It is not the frame
 * wire format, and it is not a mirror of the C++ interface.
 *
 * Conventions, each stated once here (C_ABI §3-§7):
 *
 * - Every function returns without throwing. A failure is a motion_status_t.
 * - A motion_status_t >= 0 is success; MOTION_STATUS_EMPTY is "nothing yet".
 * - Every struct starts with `struct_size`. The caller sets it to
 *   sizeof(struct) before the call. C_ABI §4 says what a smaller or larger
 *   value means. Use the MOTION_INIT macro.
 * - A `has_*` flag says whether a value is present. An absent value's storage
 *   is zero, and an absent array is NULL. Zero is never the absent spelling.
 * - A string is a motion_string_view_t of valid UTF-8. `data` is never NULL
 *   and is NUL-terminated, but `size` is the length: a value may contain NUL.
 * - A connector handle is used by one thread at a time. A frame or diagnostic
 *   handle is immutable, and may be read from any thread until it is released.
 */
#ifndef MOTIONCONNECTORCABI_MOTIONCONNECTOR_H
#define MOTIONCONNECTORCABI_MOTIONCONNECTOR_H

#include "motionConnectorCAbi/api.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ABI's own version, independent of the package version. A consumer
 * refuses a library whose major differs; a minor only appends (C_ABI §4). */
#define MOTION_ABI_VERSION_MAJOR 1u
#define MOTION_ABI_VERSION_MINOR 0u
#define MOTION_ABI_VERSION ((MOTION_ABI_VERSION_MAJOR << 16) | MOTION_ABI_VERSION_MINOR)

/* MOTION_INIT(motion_frame_info_t, info); zeroes a struct and sets its size. */
#ifdef __cplusplus
#define MOTION_INIT(type, name)                                                                    \
    type name = {};                                                                                \
    name.struct_size = static_cast<uint32_t>(sizeof(type))
#else
#define MOTION_INIT(type, name) type name = {(uint32_t)sizeof(type)}
#endif

typedef int32_t motion_status_t;
enum {
    MOTION_STATUS_OK = 0,
    /* Not a failure: no frame or diagnostic is pending. */
    MOTION_STATUS_EMPTY = 1,
    MOTION_STATUS_INVALID_ARGUMENT = -1,
    /* struct_size is below the version 1 size, or an input struct is larger
     * than this library knows, so a field it carries could not be honoured. */
    MOTION_STATUS_UNSUPPORTED_STRUCT = -2,
    MOTION_STATUS_UNKNOWN_SOURCE = -3,
    /* Open failed: a state, not a fault. The connector is in ERROR, the reason
     * is in motion_connector_last_error, and the caller may open it again. */
    MOTION_STATUS_OPEN_FAILED = -4,
    MOTION_STATUS_OUT_OF_RANGE = -5,
    /* The actor has no pose. */
    MOTION_STATUS_NOT_AVAILABLE = -6,
    MOTION_STATUS_OUT_OF_MEMORY = -7,
    /* An unexpected internal exception, stopped at the boundary. */
    MOTION_STATUS_INTERNAL = -8,
};

/* Values match CONNECTOR_CONTRACT §5. */
typedef uint32_t motion_connector_state_t;
enum {
    MOTION_STATE_DISCONNECTED = 0,
    MOTION_STATE_CONNECTING = 1,
    MOTION_STATE_CONNECTED = 2,
    MOTION_STATE_DEGRADED = 3,
    MOTION_STATE_ERROR = 4,
};

/* A bit set; values match CONNECTOR_CONTRACT §2.1. */
typedef uint32_t motion_capabilities_t;
enum {
    MOTION_CAPABILITY_BODY = 1u << 0,
    MOTION_CAPABILITY_HANDS = 1u << 1,
    MOTION_CAPABILITY_FACE = 1u << 2,
    MOTION_CAPABILITY_EYES = 1u << 3,
    MOTION_CAPABILITY_ROOT_MOTION = 1u << 4,
    MOTION_CAPABILITY_TRACKERS = 1u << 5,
    MOTION_CAPABILITY_CONTROLLERS = 1u << 6,
    MOTION_CAPABILITY_SOURCE_TIMESTAMPS = 1u << 7,
    MOTION_CAPABILITY_CONFIDENCE = 1u << 8,
    MOTION_CAPABILITY_MULTIPLE_ACTORS = 1u << 9,
};

typedef uint32_t motion_buffer_mode_t;
enum {
    MOTION_BUFFER_LATEST = 0,
    MOTION_BUFFER_ORDERED = 1,
    MOTION_BUFFER_LOSSLESS = 2,
};

/* Values match CONNECTOR_CONTRACT §6. */
typedef uint32_t motion_clock_domain_t;
enum {
    MOTION_CLOCK_DEVICE = 0,
    MOTION_CLOCK_LOCAL_MONOTONIC = 1,
    MOTION_CLOCK_WALL = 2,
    MOTION_CLOCK_NETWORK_SYNCHRONIZED = 3,
    MOTION_CLOCK_NONE = 4,
};

typedef uint32_t motion_source_kind_t;
enum {
    MOTION_SOURCE_KIND_CLIP = 0,
    MOTION_SOURCE_KIND_LIVE_CAPTURE = 1,
    MOTION_SOURCE_KIND_GENERATED = 2,
    MOTION_SOURCE_KIND_PROCEDURAL = 3,
    MOTION_SOURCE_KIND_SIMULATED = 4,
};

typedef uint8_t motion_foot_contact_t;
enum {
    MOTION_FOOT_CONTACT_UNKNOWN = 0,
    MOTION_FOOT_CONTACT_NOT_IN_CONTACT = 1,
    MOTION_FOOT_CONTACT_IN_CONTACT = 2,
};

typedef uint32_t motion_severity_t;
enum {
    MOTION_SEVERITY_INFO = 0,
    MOTION_SEVERITY_WARNING = 1,
    MOTION_SEVERITY_ERROR = 2,
};

typedef uint32_t motion_websocket_role_t;
enum {
    MOTION_WEBSOCKET_LISTEN = 0,
    MOTION_WEBSOCKET_CONNECT = 1,
};

typedef struct motion_string_view_t {
    const char* data;
    uint64_t size;
} motion_string_view_t;

typedef struct motion_connector_t motion_connector_t;
typedef struct motion_frame_t motion_frame_t;
typedef struct motion_diagnostic_t motion_diagnostic_t;

/* -- Library and vocabulary --------------------------------------------- */

/* (major << 16) | minor. */
MOTIONCONNECTORCABI_API uint32_t motion_abi_version(void);

/* The stable name of a status, "MOTION_STATUS_OK" for example, or an empty
 * string for an unknown value. Static storage. */
MOTIONCONNECTORCABI_API const char* motion_status_name(motion_status_t status);

/* The shared joint vocabulary's size and version, and a joint's stable name;
 * pose arrays are indexed by the same value. Names have static storage. */
MOTIONCONNECTORCABI_API uint64_t motion_joint_count(void);
MOTIONCONNECTORCABI_API uint32_t motion_joint_vocabulary_version(void);
MOTIONCONNECTORCABI_API motion_status_t motion_joint_name(uint64_t joint_index,
                                                          motion_string_view_t* out_name);

/* -- Sources ------------------------------------------------------------- */

/* All strings have static storage. */
typedef struct motion_source_info_t {
    uint32_t struct_size;
    motion_capabilities_t capabilities;
    /* The id motion_connector_create takes: "vmc", "mocopi", ... */
    motion_string_view_t id;
    /* The profile an open with no source_profile uses. Empty when frames carry
     * their sender's profile. */
    motion_string_view_t default_profile;
    /* The source's conventional port, 0 when it has none. Never applied
     * implicitly: motion_connector_config_t.port is always what is bound. */
    uint16_t default_port;
    uint8_t reserved[6];
} motion_source_info_t;

/* The sources this library was built with. */
MOTIONCONNECTORCABI_API uint64_t motion_source_count(void);
MOTIONCONNECTORCABI_API motion_status_t motion_source_get_info(uint64_t source_index,
                                                               motion_source_info_t* out_info);

/* -- Connector lifetime -------------------------------------------------- */

/* The WebSocket source's explicit options (WEBSOCKET_CONNECTOR §3-§6). A zero
 * numeric field selects that connector's default. */
typedef struct motion_websocket_options_t {
    uint32_t struct_size;
    motion_websocket_role_t role;
    /* NUL-terminated UTF-8, or NULL for "/". */
    const char* path;
    /* An array of allowed_origin_count NUL-terminated strings. May be NULL
     * only when the count is 0. Copied during the open. */
    const char* const* allowed_origins;
    uint64_t allowed_origin_count;
    uint64_t max_message_bytes;
    uint64_t read_budget_messages;
    double handshake_timeout_seconds;
    /* 0 is also this field's default: no silence timeout. */
    double silence_timeout_seconds;
    double reconnect_initial_seconds;
    double reconnect_max_seconds;
} motion_websocket_options_t;

/* Strings are copied during the open. */
typedef struct motion_connector_config_t {
    uint32_t struct_size;
    motion_buffer_mode_t buffer_mode;
    /* NUL-terminated UTF-8. NULL binds loopback. For a WebSocket connect role
     * it is the peer's numeric address. */
    const char* address;
    /* NULL selects the source's default profile. */
    const char* source_profile;
    /* NULL for none. */
    const char* coordinate_conversion;
    /* Must not be 0. */
    uint64_t buffer_capacity;
    /* The bound on undelivered diagnostics; 0 selects 1024. An overflow drops
     * the oldest and is counted in motion_connector_stats_t. */
    uint64_t diagnostic_capacity;
    /* NULL for the default listen role. Refused for any other source. */
    const motion_websocket_options_t* websocket;
    /* 0 asks the OS for a port; motion_connector_get_endpoint reports it. */
    uint16_t port;
    uint8_t reserved[6];
} motion_connector_config_t;

/* Creates a closed connector; *out_connector is NULL on failure. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_create(const char* source_id,
                                                                motion_connector_t** out_connector);

/* Closes and frees. NULL is ignored. Outstanding frames and diagnostics stay
 * valid: each owns its data. */
MOTIONCONNECTORCABI_API void motion_connector_destroy(motion_connector_t* connector);

/* Opening an open connector closes it first. See MOTION_STATUS_OPEN_FAILED. */
MOTIONCONNECTORCABI_API motion_status_t
motion_connector_open(motion_connector_t* connector, const motion_connector_config_t* config);

/* Idempotent. Pending frames are discarded; undelivered diagnostics remain. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_close(motion_connector_t* connector);

/* -- Connector queries --------------------------------------------------- */

MOTIONCONNECTORCABI_API motion_status_t motion_connector_get_state(
    const motion_connector_t* connector, motion_connector_state_t* out_state);

MOTIONCONNECTORCABI_API motion_status_t motion_connector_get_capabilities(
    const motion_connector_t* connector, motion_capabilities_t* out_capabilities);

/* The next three views stay valid until the next non-const call on this
 * connector, or its destruction. Copy what is kept longer. */

/* The configured profile, or the source's default before an open. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_get_source_profile(
    const motion_connector_t* connector, motion_string_view_t* out_profile);

/* The bound endpoint, or a connect role's peer. Empty before an open. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_get_endpoint(
    const motion_connector_t* connector, motion_string_view_t* out_endpoint);

/* The reason for the last failed call on this connector. A call that succeeds
 * clears it, except for a query. */
MOTIONCONNECTORCABI_API motion_status_t
motion_connector_last_error(const motion_connector_t* connector, motion_string_view_t* out_error);

typedef struct motion_connector_stats_t {
    uint32_t struct_size;
    uint32_t reserved;
    /* Cumulative buffer counters since the open (CONNECTOR_CONTRACT §8). */
    uint64_t pushed_frames;
    uint64_t polled_frames;
    uint64_t dropped_frames;
    uint64_t skipped_frames;
    uint64_t rejected_frames;
    /* Cumulative over the handle's lifetime. Frames refused at this boundary
     * for a string that is not UTF-8 (C_ABI §7), and diagnostics overflowed. */
    uint64_t refused_frames;
    uint64_t dropped_diagnostics;
} motion_connector_stats_t;

MOTIONCONNECTORCABI_API motion_status_t motion_connector_get_stats(
    const motion_connector_t* connector, motion_connector_stats_t* out_stats);

/* -- Acquisition --------------------------------------------------------- */

/* Non-blocking. Removes at most one frame. Returns OK with an owned frame,
 * or EMPTY with *out_frame NULL. A frame is independent of the connector and
 * of later polls; release it with motion_frame_release. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_poll(motion_connector_t* connector,
                                                              motion_frame_t** out_frame);

/* Non-blocking. Removes the oldest undelivered diagnostic. Returns OK with an
 * owned diagnostic, or EMPTY with *out_diagnostic NULL. Frames and diagnostics
 * are separate reads: reading one never consumes the other. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_poll_diagnostic(
    motion_connector_t* connector, motion_diagnostic_t** out_diagnostic);

/* The hardware-free input path, the one capture replay takes (C_ABI §6): one
 * received datagram or WebSocket message, through the same decode and assembly
 * as live input, with no socket. It needs no open: an unopened connector uses
 * its default buffer, which holds one frame, so drain after each push. `peer`
 * is NUL-terminated or NULL; a source that does not use it ignores it. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_push_input(motion_connector_t* connector,
                                                                    const uint8_t* bytes,
                                                                    uint64_t size,
                                                                    double receive_timestamp,
                                                                    const char* peer);

/* Completes a frame a replayed capture left open. A no-op for a source with
 * nothing to complete. */
MOTIONCONNECTORCABI_API motion_status_t motion_connector_flush_input(motion_connector_t* connector,
                                                                     double receive_timestamp);

/* -- Frame views: all valid until motion_frame_release -------------------- */

/* NULL is ignored. Releases on any thread, exactly once. */
MOTIONCONNECTORCABI_API void motion_frame_release(motion_frame_t* frame);

typedef struct motion_frame_info_t {
    uint32_t struct_size;
    motion_clock_domain_t source_clock;
    /* This connector's, monotone. */
    uint64_t frame_number;
    uint64_t actor_count;
    /* Seconds, local monotonic clock. */
    double receive_timestamp;
    /* Seconds, in the source's clock. */
    double source_timestamp;
    uint64_t sequence;
    motion_string_view_t source_profile;
    uint8_t has_source_timestamp;
    uint8_t has_sequence;
    uint8_t reserved[6];
} motion_frame_info_t;

MOTIONCONNECTORCABI_API motion_status_t motion_frame_get_info(const motion_frame_t* frame,
                                                              motion_frame_info_t* out_info);

typedef struct motion_actor_view_t {
    uint32_t struct_size;
    uint32_t reserved0;
    motion_string_view_t actor_id;
    uint64_t tracker_count;
    /* Zero without a pose. */
    uint64_t channel_count;
    uint8_t has_pose;
    uint8_t reserved1[7];
} motion_actor_view_t;

MOTIONCONNECTORCABI_API motion_status_t motion_frame_get_actor(const motion_frame_t* frame,
                                                               uint64_t actor_index,
                                                               motion_actor_view_t* out_actor);

/* The shared MotionPose, whose meaning is usd-motion-plugins' MOTION_CONTRACT.
 * A quaternion is [w, x, y, z], as in the frame wire format. A vector and a
 * quaternion are in the canonical basis, in metres where they have a length. */
typedef struct motion_pose_view_t {
    uint32_t struct_size;
    motion_source_kind_t source_kind;
    /* Seconds, in the producer's clock. */
    double timestamp;

    float root_world_position[3];
    float root_world_orientation[4];
    float root_linear_velocity[3];
    float root_angular_velocity[3];
    float look_at_target[3];

    /* joint_count entries, indexed by the vocabulary. A joint is present only
     * where valid_rotations[joint] is non-zero; an absent joint's quaternion is
     * all zeros, never identity. `confidence` is NULL when the pose has none. */
    uint64_t joint_count;
    const float (*local_rotations)[4];
    const uint8_t* valid_rotations;
    const float* confidence;

    /* Provenance: reported, never a branch condition. */
    motion_string_view_t provider;
    motion_string_view_t protocol;
    motion_string_view_t source_id;
    double metadata_source_timestamp;
    uint64_t metadata_sequence_number;

    uint8_t has_root_position;
    uint8_t has_root_orientation;
    uint8_t has_root_linear_velocity;
    uint8_t has_root_angular_velocity;
    uint8_t has_look_at_target;
    uint8_t has_contacts;
    motion_foot_contact_t left_foot_contact;
    motion_foot_contact_t right_foot_contact;
    uint8_t has_metadata_source_timestamp;
    uint8_t has_metadata_sequence_number;
    uint8_t reserved[6];
} motion_pose_view_t;

/* MOTION_STATUS_NOT_AVAILABLE when the actor has no pose. */
MOTIONCONNECTORCABI_API motion_status_t motion_frame_get_pose(const motion_frame_t* frame,
                                                              uint64_t actor_index,
                                                              motion_pose_view_t* out_pose);

typedef struct motion_channel_view_t {
    uint32_t struct_size;
    /* Not clamped: what the producer reported. */
    float value;
    motion_string_view_t name;
} motion_channel_view_t;

/* Sorted by name, each name once, as in the shared MotionChannelSet. */
MOTIONCONNECTORCABI_API motion_status_t
motion_frame_get_channel(const motion_frame_t* frame, uint64_t actor_index, uint64_t channel_index,
                         motion_channel_view_t* out_channel);

typedef struct motion_tracker_view_t {
    uint32_t struct_size;
    float confidence;
    /* As the source names it. A tracker id is not a body role. */
    motion_string_view_t tracker_id;
    float position[3];
    float rotation[4];
    uint8_t has_position;
    uint8_t has_rotation;
    uint8_t has_confidence;
    uint8_t reserved[1];
} motion_tracker_view_t;

MOTIONCONNECTORCABI_API motion_status_t
motion_frame_get_tracker(const motion_frame_t* frame, uint64_t actor_index, uint64_t tracker_index,
                         motion_tracker_view_t* out_tracker);

/* -- Diagnostic views: all valid until motion_diagnostic_release ----------- */

/* NULL is ignored. Releases on any thread, exactly once. */
MOTIONCONNECTORCABI_API void motion_diagnostic_release(motion_diagnostic_t* diagnostic);

typedef struct motion_diagnostic_view_t {
    uint32_t struct_size;
    motion_severity_t severity;
    /* The stable string, DIAGNOSTICS.md's catalog. */
    motion_string_view_t code;
    motion_string_view_t source;
    motion_string_view_t subject;
    motion_string_view_t detail;
    /* Seconds, in the source's clock. */
    double timestamp;
    uint64_t sequence;
    uint8_t recoverable;
    uint8_t has_timestamp;
    uint8_t has_sequence;
    uint8_t reserved[5];
} motion_diagnostic_view_t;

MOTIONCONNECTORCABI_API motion_status_t motion_diagnostic_get_view(
    const motion_diagnostic_t* diagnostic, motion_diagnostic_view_t* out_view);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOTIONCONNECTORCABI_MOTIONCONNECTOR_H */

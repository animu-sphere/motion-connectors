/* SPDX-License-Identifier: Apache-2.0
 *
 * A consumer in C, compiled as C and linking this library alone: the header is
 * C, and nothing but `motion_*` is needed. The layout checks freeze version 1,
 * the contract a binding that reads these structs by offset relies on. The
 * installed-consumer lane builds this same file against the installed package.
 */
#include "motionConnectorCAbi/MotionConnector.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CHECK_LAYOUT(type, size, align)                                                            \
    _Static_assert(sizeof(type) == (size), #type " size");                                         \
    _Static_assert(alignof(type) == (align), #type " alignment")
#define CHECK_FIELD(type, field, offset)                                                           \
    _Static_assert(offsetof(type, field) == (offset), #type "." #field " offset")

CHECK_LAYOUT(motion_string_view_t, 16, 8);
CHECK_LAYOUT(motion_source_info_t, 48, 8);
CHECK_FIELD(motion_source_info_t, id, 8);
CHECK_FIELD(motion_source_info_t, default_profile, 24);
CHECK_FIELD(motion_source_info_t, default_port, 40);
CHECK_LAYOUT(motion_websocket_options_t, 80, 8);
CHECK_FIELD(motion_websocket_options_t, path, 8);
CHECK_FIELD(motion_websocket_options_t, allowed_origin_count, 24);
CHECK_FIELD(motion_websocket_options_t, reconnect_max_seconds, 72);
CHECK_LAYOUT(motion_connector_config_t, 64, 8);
CHECK_FIELD(motion_connector_config_t, address, 8);
CHECK_FIELD(motion_connector_config_t, buffer_capacity, 32);
CHECK_FIELD(motion_connector_config_t, websocket, 48);
CHECK_FIELD(motion_connector_config_t, port, 56);
CHECK_LAYOUT(motion_connector_stats_t, 64, 8);
CHECK_FIELD(motion_connector_stats_t, dropped_diagnostics, 56);
CHECK_LAYOUT(motion_frame_info_t, 72, 8);
CHECK_FIELD(motion_frame_info_t, frame_number, 8);
CHECK_FIELD(motion_frame_info_t, source_profile, 48);
CHECK_FIELD(motion_frame_info_t, has_source_timestamp, 64);
CHECK_LAYOUT(motion_actor_view_t, 48, 8);
CHECK_FIELD(motion_actor_view_t, tracker_count, 24);
CHECK_FIELD(motion_actor_view_t, has_pose, 40);
CHECK_LAYOUT(motion_pose_view_t, 192, 8);
CHECK_FIELD(motion_pose_view_t, timestamp, 8);
CHECK_FIELD(motion_pose_view_t, root_world_position, 16);
CHECK_FIELD(motion_pose_view_t, look_at_target, 68);
CHECK_FIELD(motion_pose_view_t, joint_count, 80);
CHECK_FIELD(motion_pose_view_t, local_rotations, 88);
CHECK_FIELD(motion_pose_view_t, provider, 112);
CHECK_FIELD(motion_pose_view_t, metadata_sequence_number, 168);
CHECK_FIELD(motion_pose_view_t, has_root_position, 176);
CHECK_LAYOUT(motion_channel_view_t, 24, 8);
CHECK_FIELD(motion_channel_view_t, name, 8);
CHECK_LAYOUT(motion_tracker_view_t, 56, 8);
CHECK_FIELD(motion_tracker_view_t, tracker_id, 8);
CHECK_FIELD(motion_tracker_view_t, position, 24);
CHECK_FIELD(motion_tracker_view_t, rotation, 36);
CHECK_FIELD(motion_tracker_view_t, has_position, 52);
CHECK_LAYOUT(motion_diagnostic_view_t, 96, 8);
CHECK_FIELD(motion_diagnostic_view_t, code, 8);
CHECK_FIELD(motion_diagnostic_view_t, timestamp, 72);
CHECK_FIELD(motion_diagnostic_view_t, recoverable, 88);

#define REQUIRE(condition)                                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static const char message[] =
    "{\"format\":\"openstrata.motion.frame/v1\",\"frameNumber\":\"7\","
    "\"sourceProfile\":\"test.v1\",\"timing\":{\"sourceTimestamp\":1.25,"
    "\"receiveTimestamp\":1.5,\"sequence\":\"18446744073709551615\",\"sourceClock\":\"device\"},"
    "\"actors\":[{\"actor\":\"a\",\"pose\":{\"timestamp\":1.25,\"root\":{},"
    "\"joints\":{\"hips\":[1,0,0,0]},\"channels\":{\"mediapipe:jawOpen\":0.5},"
    "\"metadata\":{\"kind\":\"liveCapture\",\"provider\":\"p\",\"protocol\":\"q\",\"sourceId\":"
    "\"s\"}},"
    "\"trackers\":[{\"trackerId\":\"1\",\"position\":[1,2,3]}]}]}";

int
main(void)
{
    uint64_t index;
    int have_websocket = 0;
    motion_connector_t* connector = NULL;
    motion_frame_t* frame = NULL;

    REQUIRE(motion_abi_version() >> 16 == MOTION_ABI_VERSION_MAJOR);
    REQUIRE(motion_abi_version() == MOTION_ABI_VERSION);

    REQUIRE(motion_source_count() > 0);
    for (index = 0; index < motion_source_count(); ++index) {
        MOTION_INIT(motion_source_info_t, info);
        REQUIRE(motion_source_get_info(index, &info) == MOTION_STATUS_OK);
        if (strcmp(info.id.data, "websocket") == 0) {
            have_websocket = 1;
        }
    }
    if (!have_websocket) {
        /* A configure without WebSocket: the remainder needs its input path. */
        puts("consumed the C ABI");
        return 0;
    }

    REQUIRE(motion_connector_create("websocket", &connector) == MOTION_STATUS_OK);
    REQUIRE(motion_connector_push_input(
                connector, (const uint8_t*)message, sizeof(message) - 1, 1.5, NULL) ==
            MOTION_STATUS_OK);
    REQUIRE(motion_connector_poll(connector, &frame) == MOTION_STATUS_OK);

    {
        MOTION_INIT(motion_frame_info_t, info);
        REQUIRE(motion_frame_get_info(frame, &info) == MOTION_STATUS_OK);
        REQUIRE(info.frame_number == 1);
        REQUIRE(strcmp(info.source_profile.data, "test.v1") == 0);
        REQUIRE(info.has_source_timestamp && info.source_timestamp == 1.25);
        /* A 64-bit counter survives, at its largest. */
        REQUIRE(info.has_sequence && info.sequence == UINT64_MAX);
        REQUIRE(info.source_clock == MOTION_CLOCK_DEVICE);
        REQUIRE(info.actor_count == 1);
    }
    {
        MOTION_INIT(motion_actor_view_t, actor);
        REQUIRE(motion_frame_get_actor(frame, 0, &actor) == MOTION_STATUS_OK);
        REQUIRE(actor.actor_id.size == 1 && actor.actor_id.data[0] == 'a');
        REQUIRE(actor.has_pose && actor.tracker_count == 1 && actor.channel_count == 1);
    }
    {
        MOTION_INIT(motion_pose_view_t, pose);
        REQUIRE(motion_frame_get_pose(frame, 0, &pose) == MOTION_STATUS_OK);
        REQUIRE(pose.joint_count == motion_joint_count());
        /* Joint 0 is the first of the vocabulary, hips. */
        REQUIRE(pose.valid_rotations[0] != 0);
        REQUIRE(pose.local_rotations[0][0] == 1.0f && pose.local_rotations[0][1] == 0.0f);
        REQUIRE(pose.valid_rotations[1] == 0);
        REQUIRE(pose.local_rotations[1][0] == 0.0f);
        REQUIRE(pose.confidence == NULL);
    }
    {
        MOTION_INIT(motion_channel_view_t, channel);
        REQUIRE(motion_frame_get_channel(frame, 0, 0, &channel) == MOTION_STATUS_OK);
        REQUIRE(strcmp(channel.name.data, "mediapipe:jawOpen") == 0 && channel.value == 0.5f);
    }
    {
        MOTION_INIT(motion_tracker_view_t, tracker);
        REQUIRE(motion_frame_get_tracker(frame, 0, 0, &tracker) == MOTION_STATUS_OK);
        REQUIRE(strcmp(tracker.tracker_id.data, "1") == 0);
        REQUIRE(tracker.has_position && tracker.position[2] == 3.0f);
        REQUIRE(!tracker.has_rotation && tracker.rotation[0] == 0.0f);
    }

    motion_connector_destroy(connector);
    motion_frame_release(frame);
    puts("consumed the C ABI");
    return 0;
}

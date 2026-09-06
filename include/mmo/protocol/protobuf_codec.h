#pragma once

#include "mmo/protocol/frame_codec.h"

#include <google/protobuf/message_lite.h>

#include <vector>

namespace mmo::protocol {

std::vector<std::uint8_t> EncodeProtobufFrame(
    MessageType type,
    const google::protobuf::MessageLite& message,
    std::uint32_t max_payload_size = kDefaultMaxPayloadSize);

bool ParseProtobufPayload(
    const Frame& frame,
    MessageType expected_type,
    google::protobuf::MessageLite& message);

}  // namespace mmo::protocol

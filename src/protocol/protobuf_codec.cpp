#include "mmo/protocol/protobuf_codec.h"

#include <limits>
#include <stdexcept>

namespace mmo::protocol {

std::vector<std::uint8_t> EncodeProtobufFrame(
    MessageType type,
    const google::protobuf::MessageLite& message,
    std::uint32_t max_payload_size) {
    const auto byte_size = message.ByteSizeLong();
    if (byte_size > max_payload_size ||
        byte_size > static_cast<std::size_t>(
                        std::numeric_limits<int>::max()) ||
        byte_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Protobuf payload exceeds frame limit");
    }

    std::vector<std::uint8_t> payload(byte_size);
    if (byte_size > 0 &&
        !message.SerializeToArray(payload.data(), static_cast<int>(byte_size))) {
        throw std::runtime_error("failed to serialize Protobuf message");
    }
    return EncodeFrame(type, payload, max_payload_size);
}

bool ParseProtobufPayload(
    const Frame& frame,
    MessageType expected_type,
    google::protobuf::MessageLite& message) {
    if (frame.message_id != static_cast<std::uint32_t>(expected_type) ||
        frame.payload.size() > static_cast<std::size_t>(
                                   std::numeric_limits<int>::max())) {
        return false;
    }
    return message.ParseFromArray(
        frame.payload.data(), static_cast<int>(frame.payload.size()));
}

}  // namespace mmo::protocol

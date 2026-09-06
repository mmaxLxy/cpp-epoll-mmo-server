#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mmo::protocol {

constexpr std::size_t kFrameHeaderSize = 8;
constexpr std::uint32_t kDefaultMaxPayloadSize = 1024U * 1024U;

// IDs are intentionally defined for the independent rewrite instead of copying
// the message table from the reference project.
enum class MessageType : std::uint32_t {
    kLoginRequest = 1,
    kChatRequest = 2,
    kMoveRequest = 3,
    kHeartbeat = 4,
    kLoginResponse = 101,
    kPlayerEnter = 102,
    kPlayerLeave = 103,
    kPlayerMove = 104,
    kChatBroadcast = 105,
    kNearbyPlayers = 106,
};

bool IsKnownMessageId(std::uint32_t message_id) noexcept;

struct Frame {
    std::uint32_t message_id = 0;
    std::vector<std::uint8_t> payload;

    bool operator==(const Frame& other) const noexcept {
        return message_id == other.message_id && payload == other.payload;
    }
};

enum class DecodeStatus {
    kFrameReady,
    kNeedMoreData,
    kPayloadTooLarge,
    kUnknownMessageId,
    kDecoderFailed,
};

std::vector<std::uint8_t> EncodeFrame(
    MessageType message_type,
    const std::vector<std::uint8_t>& payload,
    std::uint32_t max_payload_size = kDefaultMaxPayloadSize);

// Incremental decoder for a TCP byte stream. The 8-byte header contains a
// network-byte-order payload length followed by a network-byte-order message ID.
class FrameDecoder {
public:
    explicit FrameDecoder(
        std::uint32_t max_payload_size = kDefaultMaxPayloadSize);

    void Append(const std::uint8_t* data, std::size_t size);
    void Append(const std::vector<std::uint8_t>& data);

    DecodeStatus Next(Frame& frame);
    void Reset() noexcept;

    std::size_t BufferedBytes() const noexcept;
    bool Failed() const noexcept;
    DecodeStatus FailureStatus() const noexcept;

private:
    DecodeStatus Fail(DecodeStatus status) noexcept;
    void Compact();

    std::uint32_t max_payload_size_;
    std::vector<std::uint8_t> buffer_;
    std::size_t read_offset_ = 0;
    DecodeStatus failure_status_ = DecodeStatus::kNeedMoreData;
    bool failed_ = false;
};

}  // namespace mmo::protocol

#include "mmo/protocol/frame_codec.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mmo::protocol {
namespace {

std::uint32_t ReadNetworkUint32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

void AppendNetworkUint32(
    std::vector<std::uint8_t>& output,
    std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

}  // namespace

bool IsKnownMessageId(std::uint32_t message_id) noexcept {
    switch (static_cast<MessageType>(message_id)) {
        case MessageType::kLoginRequest:
        case MessageType::kChatRequest:
        case MessageType::kMoveRequest:
        case MessageType::kHeartbeat:
        case MessageType::kLoginResponse:
        case MessageType::kPlayerEnter:
        case MessageType::kPlayerLeave:
        case MessageType::kPlayerMove:
        case MessageType::kChatBroadcast:
        case MessageType::kNearbyPlayers:
            return true;
    }
    return false;
}

std::vector<std::uint8_t> EncodeFrame(
    MessageType message_type,
    const std::vector<std::uint8_t>& payload,
    std::uint32_t max_payload_size) {
    const auto message_id = static_cast<std::uint32_t>(message_type);
    if (!IsKnownMessageId(message_id)) {
        throw std::invalid_argument("unknown message ID");
    }
    if (payload.size() > max_payload_size ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("payload exceeds configured maximum");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(kFrameHeaderSize + payload.size());
    AppendNetworkUint32(encoded, static_cast<std::uint32_t>(payload.size()));
    AppendNetworkUint32(encoded, message_id);
    encoded.insert(encoded.end(), payload.begin(), payload.end());
    return encoded;
}

FrameDecoder::FrameDecoder(std::uint32_t max_payload_size)
    : max_payload_size_(max_payload_size) {
    if (max_payload_size_ == 0) {
        throw std::invalid_argument("maximum payload size must be positive");
    }
}

void FrameDecoder::Append(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return;
    }
    if (data == nullptr) {
        throw std::invalid_argument("data must not be null when size is positive");
    }
    buffer_.insert(buffer_.end(), data, data + size);
}

void FrameDecoder::Append(const std::vector<std::uint8_t>& data) {
    Append(data.data(), data.size());
}

DecodeStatus FrameDecoder::Next(Frame& frame) {
    if (failed_) {
        return DecodeStatus::kDecoderFailed;
    }

    const std::size_t available = BufferedBytes();
    if (available < kFrameHeaderSize) {
        return DecodeStatus::kNeedMoreData;
    }

    const auto* header = buffer_.data() + read_offset_;
    const std::uint32_t payload_size = ReadNetworkUint32(header);
    const std::uint32_t message_id = ReadNetworkUint32(header + 4);

    if (payload_size > max_payload_size_) {
        return Fail(DecodeStatus::kPayloadTooLarge);
    }
    if (!IsKnownMessageId(message_id)) {
        return Fail(DecodeStatus::kUnknownMessageId);
    }

    const std::size_t frame_size = kFrameHeaderSize + payload_size;
    if (available < frame_size) {
        return DecodeStatus::kNeedMoreData;
    }

    frame.message_id = message_id;
    const auto payload_begin = buffer_.begin() +
                               static_cast<std::ptrdiff_t>(read_offset_ + kFrameHeaderSize);
    frame.payload.assign(
        payload_begin,
        payload_begin + static_cast<std::ptrdiff_t>(payload_size));
    read_offset_ += frame_size;
    Compact();
    return DecodeStatus::kFrameReady;
}

void FrameDecoder::Reset() noexcept {
    buffer_.clear();
    read_offset_ = 0;
    failed_ = false;
    failure_status_ = DecodeStatus::kNeedMoreData;
}

std::size_t FrameDecoder::BufferedBytes() const noexcept {
    return buffer_.size() - read_offset_;
}

bool FrameDecoder::Failed() const noexcept {
    return failed_;
}

DecodeStatus FrameDecoder::FailureStatus() const noexcept {
    return failure_status_;
}

DecodeStatus FrameDecoder::Fail(DecodeStatus status) noexcept {
    failed_ = true;
    failure_status_ = status;
    return status;
}

void FrameDecoder::Compact() {
    if (read_offset_ == buffer_.size()) {
        buffer_.clear();
        read_offset_ = 0;
        return;
    }

    if (read_offset_ >= 4096 && read_offset_ * 2 >= buffer_.size()) {
        buffer_.erase(
            buffer_.begin(),
            buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
        read_offset_ = 0;
    }
}

}  // namespace mmo::protocol

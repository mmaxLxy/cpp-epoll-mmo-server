#include "mmo/protocol/frame_codec.h"

#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {

using mmo::protocol::DecodeStatus;
using mmo::protocol::EncodeFrame;
using mmo::protocol::Frame;
using mmo::protocol::FrameDecoder;
using mmo::protocol::MessageType;

void EncodeUsesNetworkByteOrder() {
    const std::vector<std::uint8_t> payload{0xaa, 0xbb, 0xcc};
    const auto encoded = EncodeFrame(MessageType::kMoveRequest, payload);
    const std::vector<std::uint8_t> expected{
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x03,
        0xaa, 0xbb, 0xcc,
    };
    CHECK_EQ(encoded, expected);
}

void FragmentedFrameWaitsForAllBytes() {
    const std::vector<std::uint8_t> payload{1, 2, 3, 4, 5};
    const auto encoded = EncodeFrame(MessageType::kChatRequest, payload);
    FrameDecoder decoder;
    Frame decoded;

    decoder.Append(encoded.data(), 3);
    CHECK_EQ(decoder.Next(decoded), DecodeStatus::kNeedMoreData);

    decoder.Append(encoded.data() + 3, 5);
    CHECK_EQ(decoder.Next(decoded), DecodeStatus::kNeedMoreData);

    decoder.Append(encoded.data() + 8, encoded.size() - 8);
    CHECK_EQ(decoder.Next(decoded), DecodeStatus::kFrameReady);
    CHECK_EQ(decoded.message_id,
             static_cast<std::uint32_t>(MessageType::kChatRequest));
    CHECK_EQ(decoded.payload, payload);
    CHECK_EQ(decoder.Next(decoded), DecodeStatus::kNeedMoreData);
    CHECK_EQ(decoder.BufferedBytes(), 0U);
}

void StickyFramesAreDecodedOneByOne() {
    const auto first = EncodeFrame(
        MessageType::kHeartbeat, std::vector<std::uint8_t>{});
    const auto second = EncodeFrame(
        MessageType::kMoveRequest,
        std::vector<std::uint8_t>{9, 8, 7});

    std::vector<std::uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());

    FrameDecoder decoder;
    decoder.Append(bytes);
    Frame frame;

    CHECK_EQ(decoder.Next(frame), DecodeStatus::kFrameReady);
    CHECK_EQ(frame.message_id,
             static_cast<std::uint32_t>(MessageType::kHeartbeat));
    CHECK_TRUE(frame.payload.empty());

    CHECK_EQ(decoder.Next(frame), DecodeStatus::kFrameReady);
    CHECK_EQ(frame.message_id,
             static_cast<std::uint32_t>(MessageType::kMoveRequest));
    CHECK_EQ(frame.payload, std::vector<std::uint8_t>({9, 8, 7}));
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kNeedMoreData);
}

void OversizedPayloadIsRejectedFromHeader() {
    FrameDecoder decoder(16);
    const std::vector<std::uint8_t> header{
        0x00, 0x00, 0x00, 0x11,
        0x00, 0x00, 0x00, 0x03,
    };
    decoder.Append(header);
    Frame frame;

    CHECK_EQ(decoder.Next(frame), DecodeStatus::kPayloadTooLarge);
    CHECK_TRUE(decoder.Failed());
    CHECK_EQ(decoder.FailureStatus(), DecodeStatus::kPayloadTooLarge);
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kDecoderFailed);
}

void UnknownMessageIdIsRejected() {
    FrameDecoder decoder;
    const std::vector<std::uint8_t> header{
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x03, 0xe7,
    };
    decoder.Append(header);
    Frame frame;

    CHECK_EQ(decoder.Next(frame), DecodeStatus::kUnknownMessageId);
    CHECK_TRUE(decoder.Failed());
}

void ResetAllowsReuseAfterProtocolError() {
    FrameDecoder decoder(4);
    decoder.Append(std::vector<std::uint8_t>{
        0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x01,
    });
    Frame frame;
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kPayloadTooLarge);

    decoder.Reset();
    const auto encoded = EncodeFrame(
        MessageType::kLoginRequest,
        std::vector<std::uint8_t>{1, 2},
        4);
    decoder.Append(encoded);
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kFrameReady);
    CHECK_EQ(frame.payload, std::vector<std::uint8_t>({1, 2}));
}

}  // namespace

int main() {
    return test_support::Run({
        {"encode uses network byte order", EncodeUsesNetworkByteOrder},
        {"fragmented frame waits for all bytes", FragmentedFrameWaitsForAllBytes},
        {"sticky frames decode one by one", StickyFramesAreDecodedOneByOne},
        {"oversized payload is rejected", OversizedPayloadIsRejectedFromHeader},
        {"unknown message ID is rejected", UnknownMessageIdIsRejected},
        {"decoder can reset after an error", ResetAllowsReuseAfterProtocolError},
    });
}

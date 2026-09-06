#include "mmo/protocol/frame_codec.h"
#include "mmo/protocol/protobuf_codec.h"

#include "game_messages.pb.h"
#include "test_support.h"

namespace {

using mmo::protocol::DecodeStatus;
using mmo::protocol::EncodeProtobufFrame;
using mmo::protocol::Frame;
using mmo::protocol::FrameDecoder;
using mmo::protocol::MessageType;
using mmo::protocol::ParseProtobufPayload;

void MessageRoundTripPreservesFields() {
    mmo::proto::LoginRequest source;
    source.set_account("account-42");
    source.set_session_token("token-42");

    const auto bytes = EncodeProtobufFrame(MessageType::kLoginRequest, source);
    FrameDecoder decoder;
    decoder.Append(bytes);
    Frame frame;
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kFrameReady);

    mmo::proto::LoginRequest parsed;
    CHECK_TRUE(ParseProtobufPayload(
        frame, MessageType::kLoginRequest, parsed));
    CHECK_EQ(parsed.account(), source.account());
    CHECK_EQ(parsed.session_token(), source.session_token());
}

void WrongMessageTypeIsRejected() {
    mmo::proto::ChatRequest source;
    source.set_text("hello");
    const auto bytes = EncodeProtobufFrame(MessageType::kChatRequest, source);
    FrameDecoder decoder;
    decoder.Append(bytes);
    Frame frame;
    CHECK_EQ(decoder.Next(frame), DecodeStatus::kFrameReady);

    mmo::proto::ChatRequest parsed;
    CHECK_FALSE(ParseProtobufPayload(
        frame, MessageType::kMoveRequest, parsed));
}

void MalformedPayloadIsRejected() {
    Frame frame{
        static_cast<std::uint32_t>(MessageType::kLoginRequest),
        {0x0A, 0x05, 'a'}};
    mmo::proto::LoginRequest parsed;
    CHECK_FALSE(ParseProtobufPayload(
        frame, MessageType::kLoginRequest, parsed));
}

}  // namespace

int main() {
    return test_support::Run({
        {"Protobuf message round trip", MessageRoundTripPreservesFields},
        {"wrong message type is rejected", WrongMessageTypeIsRejected},
        {"malformed Protobuf payload is rejected", MalformedPayloadIsRejected},
    });
}

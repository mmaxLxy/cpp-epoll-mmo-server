#include "mmo/client/blocking_client.h"
#include "mmo/protocol/protobuf_codec.h"

#include "game_messages.pb.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

void PrintFrame(const mmo::protocol::Frame& frame) {
    const auto type = static_cast<mmo::protocol::MessageType>(frame.message_id);
    if (type == mmo::protocol::MessageType::kLoginResponse) {
        mmo::proto::LoginResponse message;
        if (mmo::protocol::ParseProtobufPayload(frame, type, message)) {
            if (message.success()) {
                std::cout << "login ok: id=" << message.self().player_id()
                          << " nickname=" << message.self().nickname()
                          << " token=" << message.session_token() << '\n';
            } else {
                std::cout << "login failed: " << message.error() << '\n';
            }
        }
    } else if (type == mmo::protocol::MessageType::kChatBroadcast) {
        mmo::proto::ChatBroadcast message;
        if (mmo::protocol::ParseProtobufPayload(frame, type, message)) {
            std::cout << message.nickname() << ": " << message.text() << '\n';
        }
    } else if (type == mmo::protocol::MessageType::kPlayerEnter) {
        mmo::proto::PlayerEnter message;
        if (mmo::protocol::ParseProtobufPayload(frame, type, message)) {
            std::cout << "player entered: " << message.player().nickname() << '\n';
        }
    } else if (type == mmo::protocol::MessageType::kPlayerLeave) {
        mmo::proto::PlayerLeave message;
        if (mmo::protocol::ParseProtobufPayload(frame, type, message)) {
            std::cout << "player left: " << message.player_id() << '\n';
        }
    } else if (type == mmo::protocol::MessageType::kPlayerMove) {
        mmo::proto::PlayerMove message;
        if (mmo::protocol::ParseProtobufPayload(frame, type, message)) {
            std::cout << "player moved: " << message.player_id() << " -> ("
                      << message.position().x() << ", "
                      << message.position().y() << ")\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: mmo_client HOST PORT ACCOUNT [SESSION_TOKEN]\n";
        return 1;
    }

    mmo::client::BlockingClient client;
    if (!client.Connect(argv[1], static_cast<std::uint16_t>(std::stoul(argv[2])))) {
        std::cerr << "failed to connect\n";
        return 1;
    }

    mmo::proto::LoginRequest login;
    login.set_account(argv[3]);
    if (argc == 5) {
        login.set_session_token(argv[4]);
    }
    if (!client.Send(mmo::protocol::MessageType::kLoginRequest, login)) {
        std::cerr << "failed to send login\n";
        return 1;
    }

    std::atomic<bool> running{true};
    std::thread receiver([&] {
        while (running.load()) {
            mmo::protocol::Frame frame;
            const auto status = client.Receive(frame, std::chrono::milliseconds(250));
            if (status == mmo::client::ReceiveStatus::kFrameReady) {
                PrintFrame(frame);
            } else if (status != mmo::client::ReceiveStatus::kTimeout) {
                running.store(false);
            }
        }
    });

    std::cout << "commands: chat TEXT | move X Y | heartbeat | quit\n";
    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command == "quit") {
            break;
        }
        if (command == "chat") {
            std::string text;
            std::getline(input >> std::ws, text);
            mmo::proto::ChatRequest request;
            request.set_text(std::move(text));
            client.Send(mmo::protocol::MessageType::kChatRequest, request);
        } else if (command == "move") {
            double x = 0.0;
            double y = 0.0;
            if (input >> x >> y) {
                mmo::proto::MoveRequest request;
                request.mutable_position()->set_x(x);
                request.mutable_position()->set_y(y);
                client.Send(mmo::protocol::MessageType::kMoveRequest, request);
            }
        } else if (command == "heartbeat") {
            mmo::proto::Heartbeat heartbeat;
            client.Send(mmo::protocol::MessageType::kHeartbeat, heartbeat);
        }
    }

    running.store(false);
    client.Shutdown();
    receiver.join();
    return 0;
}

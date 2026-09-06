#include "mmo/data/redis_session_store.h"

#include <hiredis/hiredis.h>

#include <memory>
#include <utility>

namespace mmo::data {
namespace {

using RedisConnection = std::unique_ptr<redisContext, decltype(&redisFree)>;
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

RedisConnection Connect(const RedisConfig& config, std::string& error) {
    const auto timeout_count = config.connect_timeout.count();
    timeval timeout{};
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(timeout_count / 1000);
    timeout.tv_usec =
        static_cast<decltype(timeout.tv_usec)>((timeout_count % 1000) * 1000);

    RedisConnection connection(
        redisConnectWithTimeout(config.host.c_str(), config.port, timeout),
        redisFree);
    if (!connection) {
        error = "redisConnectWithTimeout failed";
        return connection;
    }
    if (connection->err != 0) {
        error = connection->errstr;
        return RedisConnection(nullptr, redisFree);
    }
    return connection;
}

OperationResult Unavailable(std::string message) {
    return OperationResult::Failure(
        DataStatus::kUnavailable, std::move(message));
}

}  // namespace

RedisSessionStore::RedisSessionStore(RedisConfig config)
    : config_(std::move(config)) {}

OperationResult RedisSessionStore::Store(
    const SessionRecord& session,
    std::chrono::seconds ttl) {
    if (session.player_id == 0 || session.token.empty() || ttl.count() <= 0) {
        return OperationResult::Failure(
            DataStatus::kInvalidArgument,
            "player_id, token and positive ttl are required");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return Unavailable(std::move(error));
    }

    const std::string key = KeyFor(session.player_id);
    RedisReply reply(
        static_cast<redisReply*>(redisCommand(
            connection.get(),
            "SET %b %b EX %lld",
            key.data(),
            key.size(),
            session.token.data(),
            session.token.size(),
            static_cast<long long>(ttl.count()))),
        freeReplyObject);
    if (!reply) {
        return Unavailable(connection->errstr);
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        return Unavailable(reply->str == nullptr ? "Redis SET failed" : reply->str);
    }
    if (reply->type != REDIS_REPLY_STATUS || reply->str == nullptr ||
        std::string(reply->str, reply->len) != "OK") {
        return Unavailable("Redis SET returned an unexpected reply");
    }
    return OperationResult::Success();
}

DataResult<SessionRecord> RedisSessionStore::Load(std::uint64_t player_id) {
    if (player_id == 0) {
        return DataResult<SessionRecord>::Failure(
            DataStatus::kInvalidArgument, "player_id must be greater than zero");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return DataResult<SessionRecord>::Failure(
            DataStatus::kUnavailable, std::move(error));
    }

    const std::string key = KeyFor(player_id);
    RedisReply reply(
        static_cast<redisReply*>(redisCommand(
            connection.get(), "GET %b", key.data(), key.size())),
        freeReplyObject);
    if (!reply) {
        return DataResult<SessionRecord>::Failure(
            DataStatus::kUnavailable, connection->errstr);
    }
    if (reply->type == REDIS_REPLY_NIL) {
        return DataResult<SessionRecord>::NotFound();
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        return DataResult<SessionRecord>::Failure(
            DataStatus::kUnavailable,
            reply->str == nullptr ? "Redis GET failed" : reply->str);
    }
    if (reply->type != REDIS_REPLY_STRING || reply->str == nullptr) {
        return DataResult<SessionRecord>::Failure(
            DataStatus::kUnavailable, "Redis GET returned an unexpected reply");
    }

    return DataResult<SessionRecord>::Success(
        SessionRecord{player_id, std::string(reply->str, reply->len)});
}

OperationResult RedisSessionStore::Remove(std::uint64_t player_id) {
    if (player_id == 0) {
        return OperationResult::Failure(
            DataStatus::kInvalidArgument, "player_id must be greater than zero");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return Unavailable(std::move(error));
    }

    const std::string key = KeyFor(player_id);
    RedisReply reply(
        static_cast<redisReply*>(redisCommand(
            connection.get(), "DEL %b", key.data(), key.size())),
        freeReplyObject);
    if (!reply) {
        return Unavailable(connection->errstr);
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        return Unavailable(reply->str == nullptr ? "Redis DEL failed" : reply->str);
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        return Unavailable("Redis DEL returned an unexpected reply");
    }
    return OperationResult::Success();
}

std::string RedisSessionStore::KeyFor(std::uint64_t player_id) const {
    return config_.key_prefix + std::to_string(player_id);
}

}  // namespace mmo::data

#include "mmo/data/mysql_player_repository.h"

#include <mysql/mysql.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mmo::data {
namespace {

using Connection = std::unique_ptr<MYSQL, decltype(&mysql_close)>;
using Statement = std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;

class MySqlLibrary {
public:
    MySqlLibrary() {
        if (mysql_library_init(0, nullptr, nullptr) != 0) {
            throw std::runtime_error("mysql_library_init failed");
        }
    }

    ~MySqlLibrary() { mysql_library_end(); }
};

void EnsureMySqlLibraryInitialized() {
    static const MySqlLibrary library;
    (void)library;
}

Connection Connect(const MySqlConfig& config, std::string& error) {
    Connection connection(mysql_init(nullptr), mysql_close);
    if (!connection) {
        error = "mysql_init failed";
        return connection;
    }

    mysql_options(
        connection.get(),
        MYSQL_OPT_CONNECT_TIMEOUT,
        &config.connect_timeout_seconds);

    if (mysql_real_connect(
            connection.get(),
            config.host.c_str(),
            config.user.c_str(),
            config.password.c_str(),
            config.database.c_str(),
            config.port,
            nullptr,
            0) == nullptr) {
        error = mysql_error(connection.get());
        return Connection(nullptr, mysql_close);
    }

    if (mysql_set_character_set(connection.get(), "utf8mb4") != 0) {
        error = mysql_error(connection.get());
        return Connection(nullptr, mysql_close);
    }
    return connection;
}

Statement Prepare(MYSQL* connection, const char* sql, std::string& error) {
    Statement statement(mysql_stmt_init(connection), mysql_stmt_close);
    if (!statement) {
        error = "mysql_stmt_init failed";
        return statement;
    }
    if (mysql_stmt_prepare(statement.get(), sql, std::strlen(sql)) != 0) {
        error = mysql_stmt_error(statement.get());
        return Statement(nullptr, mysql_stmt_close);
    }
    return statement;
}

OperationResult Invalid(std::string message) {
    return OperationResult::Failure(
        DataStatus::kInvalidArgument, std::move(message));
}

OperationResult Unavailable(std::string message) {
    return OperationResult::Failure(
        DataStatus::kUnavailable, std::move(message));
}

}  // namespace

MySqlPlayerRepository::MySqlPlayerRepository(MySqlConfig config)
    : config_(std::move(config)) {
    EnsureMySqlLibraryInitialized();
}

OperationResult MySqlPlayerRepository::EnsureSchema() {
    if (config_.database.empty()) {
        return Invalid("MySQL database must not be empty");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return Unavailable(std::move(error));
    }

    constexpr const char* kCreateTable =
        "CREATE TABLE IF NOT EXISTS players ("
        "player_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "account VARCHAR(64) NOT NULL UNIQUE,"
        "nickname VARCHAR(64) NOT NULL,"
        "position_x DOUBLE NOT NULL,"
        "position_y DOUBLE NOT NULL,"
        "position_z DOUBLE NOT NULL,"
        "last_login_unix BIGINT NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(connection.get(), kCreateTable) != 0) {
        return Unavailable(mysql_error(connection.get()));
    }
    return OperationResult::Success();
}

DataResult<PlayerRecord> MySqlPlayerRepository::LoadByAccount(
    const std::string& account) {
    if (account.empty() || account.size() > 64) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kInvalidArgument,
            "account length must be between 1 and 64 bytes");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, std::move(error));
    }

    constexpr const char* kSelect =
        "SELECT player_id, account, nickname, position_x, position_y, "
        "position_z, last_login_unix FROM players WHERE account = ?";
    auto statement = Prepare(connection.get(), kSelect, error);
    if (!statement) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, std::move(error));
    }

    unsigned long account_length = static_cast<unsigned long>(account.size());
    MYSQL_BIND input{};
    input.buffer_type = MYSQL_TYPE_STRING;
    input.buffer = const_cast<char*>(account.data());
    input.buffer_length = account_length;
    input.length = &account_length;
    if (mysql_stmt_bind_param(statement.get(), &input) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, mysql_stmt_error(statement.get()));
    }

    PlayerRecord player;
    std::array<char, 65> account_buffer{};
    std::array<char, 65> nickname_buffer{};
    unsigned long loaded_account_length = 0;
    unsigned long nickname_length = 0;
    bool player_id_is_unsigned = true;

    std::array<MYSQL_BIND, 7> output{};
    output[0].buffer_type = MYSQL_TYPE_LONGLONG;
    output[0].buffer = &player.player_id;
    output[0].is_unsigned = player_id_is_unsigned;
    output[1].buffer_type = MYSQL_TYPE_STRING;
    output[1].buffer = account_buffer.data();
    output[1].buffer_length = account_buffer.size();
    output[1].length = &loaded_account_length;
    output[2].buffer_type = MYSQL_TYPE_STRING;
    output[2].buffer = nickname_buffer.data();
    output[2].buffer_length = nickname_buffer.size();
    output[2].length = &nickname_length;
    output[3].buffer_type = MYSQL_TYPE_DOUBLE;
    output[3].buffer = &player.position_x;
    output[4].buffer_type = MYSQL_TYPE_DOUBLE;
    output[4].buffer = &player.position_y;
    output[5].buffer_type = MYSQL_TYPE_DOUBLE;
    output[5].buffer = &player.position_z;
    output[6].buffer_type = MYSQL_TYPE_LONGLONG;
    output[6].buffer = &player.last_login_unix;

    if (mysql_stmt_bind_result(statement.get(), output.data()) != 0 ||
        mysql_stmt_store_result(statement.get()) != 0) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, mysql_stmt_error(statement.get()));
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        return DataResult<PlayerRecord>::NotFound();
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, mysql_stmt_error(statement.get()));
    }
    if (loaded_account_length > 64 || nickname_length > 64) {
        return DataResult<PlayerRecord>::Failure(
            DataStatus::kUnavailable, "stored player text exceeds schema limit");
    }

    player.account.assign(account_buffer.data(), loaded_account_length);
    player.nickname.assign(nickname_buffer.data(), nickname_length);
    return DataResult<PlayerRecord>::Success(std::move(player));
}

OperationResult MySqlPlayerRepository::Save(const PlayerRecord& player) {
    if (player.player_id == 0) {
        return Invalid("player_id must be greater than zero");
    }
    if (player.account.empty() || player.account.size() > 64) {
        return Invalid("account length must be between 1 and 64 bytes");
    }
    if (player.nickname.empty() || player.nickname.size() > 64) {
        return Invalid("nickname length must be between 1 and 64 bytes");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return Unavailable(std::move(error));
    }

    constexpr const char* kUpsert =
        "INSERT INTO players (player_id, account, nickname, position_x, "
        "position_y, position_z, last_login_unix) VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE player_id=VALUES(player_id), "
        "nickname=VALUES(nickname), position_x=VALUES(position_x), "
        "position_y=VALUES(position_y), position_z=VALUES(position_z), "
        "last_login_unix=VALUES(last_login_unix)";
    auto statement = Prepare(connection.get(), kUpsert, error);
    if (!statement) {
        return Unavailable(std::move(error));
    }

    auto player_id = player.player_id;
    auto position_x = player.position_x;
    auto position_y = player.position_y;
    auto position_z = player.position_z;
    auto last_login_unix = player.last_login_unix;
    unsigned long account_length =
        static_cast<unsigned long>(player.account.size());
    unsigned long nickname_length =
        static_cast<unsigned long>(player.nickname.size());

    std::array<MYSQL_BIND, 7> input{};
    input[0].buffer_type = MYSQL_TYPE_LONGLONG;
    input[0].buffer = &player_id;
    input[0].is_unsigned = true;
    input[1].buffer_type = MYSQL_TYPE_STRING;
    input[1].buffer = const_cast<char*>(player.account.data());
    input[1].buffer_length = account_length;
    input[1].length = &account_length;
    input[2].buffer_type = MYSQL_TYPE_STRING;
    input[2].buffer = const_cast<char*>(player.nickname.data());
    input[2].buffer_length = nickname_length;
    input[2].length = &nickname_length;
    input[3].buffer_type = MYSQL_TYPE_DOUBLE;
    input[3].buffer = &position_x;
    input[4].buffer_type = MYSQL_TYPE_DOUBLE;
    input[4].buffer = &position_y;
    input[5].buffer_type = MYSQL_TYPE_DOUBLE;
    input[5].buffer = &position_z;
    input[6].buffer_type = MYSQL_TYPE_LONGLONG;
    input[6].buffer = &last_login_unix;

    if (mysql_stmt_bind_param(statement.get(), input.data()) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        return Unavailable(mysql_stmt_error(statement.get()));
    }
    return OperationResult::Success();
}

OperationResult MySqlPlayerRepository::DeleteByAccount(
    const std::string& account) {
    if (account.empty() || account.size() > 64) {
        return Invalid("account length must be between 1 and 64 bytes");
    }

    std::string error;
    auto connection = Connect(config_, error);
    if (!connection) {
        return Unavailable(std::move(error));
    }

    constexpr const char* kDelete = "DELETE FROM players WHERE account = ?";
    auto statement = Prepare(connection.get(), kDelete, error);
    if (!statement) {
        return Unavailable(std::move(error));
    }

    unsigned long account_length = static_cast<unsigned long>(account.size());
    MYSQL_BIND input{};
    input.buffer_type = MYSQL_TYPE_STRING;
    input.buffer = const_cast<char*>(account.data());
    input.buffer_length = account_length;
    input.length = &account_length;
    if (mysql_stmt_bind_param(statement.get(), &input) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        return Unavailable(mysql_stmt_error(statement.get()));
    }
    return OperationResult::Success();
}

}  // namespace mmo::data

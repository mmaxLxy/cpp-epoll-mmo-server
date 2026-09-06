CREATE TABLE IF NOT EXISTS players (
    player_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    account VARCHAR(64) NOT NULL UNIQUE,
    nickname VARCHAR(64) NOT NULL,
    position_x DOUBLE NOT NULL,
    position_y DOUBLE NOT NULL,
    position_z DOUBLE NOT NULL,
    last_login_unix BIGINT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

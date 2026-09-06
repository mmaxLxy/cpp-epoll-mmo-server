cmake_minimum_required(VERSION 3.20)

project(cpp_epoll_mmo_server LANGUAGES CXX)

find_package(Threads REQUIRED)

option(MMO_ENABLE_DATABASES "Build MySQL and Redis adapters" OFF)
option(MMO_BUILD_SERVER "Build the Linux epoll server and client" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mmo_core
    src/concurrency/bounded_thread_pool.cpp
    src/concurrency/completion_queue.cpp
    src/data/async_data_service.cpp
    src/data/memory_stores.cpp
    src/game/aoi_world.cpp
    src/protocol/frame_codec.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(
        mmo_core PRIVATE src/concurrency/event_fd_wakeup.cpp
    )
endif()

target_include_directories(mmo_core PUBLIC include)
target_link_libraries(mmo_core PUBLIC Threads::Threads)

if(MMO_BUILD_SERVER)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "MMO_BUILD_SERVER requires Linux")
    endif()

    find_package(Protobuf REQUIRED)
    protobuf_generate_cpp(
        MMO_PROTO_SOURCES
        MMO_PROTO_HEADERS
        proto/game_messages.proto
    )
    add_library(mmo_messages ${MMO_PROTO_SOURCES} ${MMO_PROTO_HEADERS})
    target_include_directories(
        mmo_messages PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_link_libraries(mmo_messages PUBLIC protobuf::libprotobuf)

    add_library(mmo_protocol src/protocol/protobuf_codec.cpp)
    target_include_directories(mmo_protocol PUBLIC include)
    target_link_libraries(mmo_protocol PUBLIC mmo_core mmo_messages)

    add_library(mmo_game src/game/game_service.cpp)
    target_include_directories(mmo_game PUBLIC include)
    target_link_libraries(mmo_game PUBLIC mmo_protocol)

    add_library(
        mmo_network
        src/net/event_loop.cpp
        src/net/tcp_connection.cpp
    )
    target_include_directories(mmo_network PUBLIC include)
    target_link_libraries(mmo_network PUBLIC mmo_core)

    add_library(mmo_server_lib src/server/game_server.cpp)
    target_include_directories(mmo_server_lib PUBLIC include)
    target_link_libraries(
        mmo_server_lib PUBLIC mmo_network mmo_game mmo_protocol
    )

    add_executable(mmo_server apps/mmo_server_main.cpp)
    target_link_libraries(mmo_server PRIVATE mmo_server_lib)

    add_library(mmo_client_lib src/client/blocking_client.cpp)
    target_include_directories(mmo_client_lib PUBLIC include)
    target_link_libraries(mmo_client_lib PUBLIC mmo_protocol)

    add_executable(mmo_client apps/mmo_client_main.cpp)
    target_link_libraries(mmo_client PRIVATE mmo_client_lib)
endif()

if(MMO_ENABLE_DATABASES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)
    pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

    add_library(mmo_database
        src/data/mysql_player_repository.cpp
        src/data/redis_session_store.cpp
    )
    target_include_directories(mmo_database PUBLIC include)
    target_link_libraries(mmo_database
        PUBLIC
            mmo_core
            PkgConfig::MYSQLCLIENT
            PkgConfig::HIREDIS
    )

    if(MMO_BUILD_SERVER)
        target_link_libraries(mmo_server PRIVATE mmo_database)
        target_compile_definitions(mmo_server PRIVATE MMO_HAS_DATABASES=1)
    endif()

endif()

add_executable(mmo_bench tools/mmo_bench.cpp)
target_link_libraries(mmo_bench PRIVATE mmo_core)

if(WIN32)
    target_link_libraries(mmo_bench PRIVATE psapi)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(protocol_tests PRIVATE mmo_core)

    add_executable(aoi_tests tests/aoi_tests.cpp)
    target_link_libraries(aoi_tests PRIVATE mmo_core)

    add_executable(data_async_tests tests/data_async_tests.cpp)
    target_link_libraries(data_async_tests PRIVATE mmo_core)

    add_test(NAME protocol_tests COMMAND protocol_tests)
    add_test(NAME aoi_tests COMMAND aoi_tests)
    add_test(NAME data_async_tests COMMAND data_async_tests)

    if(MMO_BUILD_SERVER)
        add_executable(
            protobuf_codec_tests tests/protobuf_codec_tests.cpp
        )
        target_link_libraries(protobuf_codec_tests PRIVATE mmo_protocol)
        add_test(NAME protobuf_codec_tests COMMAND protobuf_codec_tests)

        add_executable(network_core_tests tests/network_core_tests.cpp)
        target_link_libraries(network_core_tests PRIVATE mmo_network)
        add_test(NAME network_core_tests COMMAND network_core_tests)

        add_executable(game_service_tests tests/game_service_tests.cpp)
        target_link_libraries(game_service_tests PRIVATE mmo_game)
        add_test(NAME game_service_tests COMMAND game_service_tests)

        add_executable(
            server_integration_tests tests/server_integration_tests.cpp
        )
        target_link_libraries(
            server_integration_tests PRIVATE mmo_server_lib mmo_client_lib
        )
        add_test(
            NAME server_integration_tests COMMAND server_integration_tests
        )
        set_tests_properties(
            server_integration_tests PROPERTIES TIMEOUT 15
        )
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(
            event_fd_completion_tests
            tests/event_fd_completion_tests.cpp
        )
        target_link_libraries(event_fd_completion_tests PRIVATE mmo_core)
        add_test(
            NAME event_fd_completion_tests
            COMMAND event_fd_completion_tests
        )
    endif()

    if(MMO_ENABLE_DATABASES)
        add_executable(
            database_integration_tests
            tests/database_integration_tests.cpp
        )
        target_link_libraries(
            database_integration_tests PRIVATE mmo_database
        )
        add_test(
            NAME database_integration_tests
            COMMAND database_integration_tests
        )

        if(MMO_BUILD_SERVER)
            add_executable(
                database_server_integration_tests
                tests/database_server_integration_tests.cpp
            )
            target_link_libraries(
                database_server_integration_tests
                PRIVATE mmo_server_lib mmo_client_lib mmo_database
            )
            add_test(
                NAME database_server_integration_tests
                COMMAND database_server_integration_tests
            )
            set_tests_properties(
                database_server_integration_tests PROPERTIES TIMEOUT 20
            )
        endif()
    endif()
endif()

set(MMO_WARNING_TARGETS
    mmo_core
    mmo_protocol
    mmo_game
    mmo_network
    mmo_server_lib
    mmo_server
    mmo_client_lib
    mmo_client
    mmo_database
    mmo_bench
    protocol_tests
    aoi_tests
    data_async_tests
    protobuf_codec_tests
    network_core_tests
    game_service_tests
    server_integration_tests
    event_fd_completion_tests
    database_integration_tests
    database_server_integration_tests
)

foreach(target IN LISTS MMO_WARNING_TARGETS)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()

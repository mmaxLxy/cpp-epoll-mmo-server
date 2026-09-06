#include "mmo/game/aoi_world.h"
#include "mmo/protocol/frame_codec.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using mmo::game::AoiConfig;
using mmo::game::AoiWorld;
using mmo::game::Position;
using mmo::protocol::DecodeStatus;
using mmo::protocol::EncodeFrame;
using mmo::protocol::Frame;
using mmo::protocol::FrameDecoder;
using mmo::protocol::MessageType;

struct Options {
    std::size_t players = 1000;
    std::size_t moves_per_player = 20;
    std::size_t repetitions = 3;
    std::size_t payload_bytes = 32;
    std::string distribution = "uniform";
    std::uint64_t seed = 20260902;
    std::string output_path;
};

struct ProtocolMetrics {
    std::size_t frames = 0;
    std::size_t bytes = 0;
    double encode_wall_ms = 0.0;
    double encode_cpu_ms = 0.0;
    double decode_wall_ms = 0.0;
    double decode_cpu_ms = 0.0;
    double encode_frames_per_second = 0.0;
    double decode_frames_per_second = 0.0;
};

struct AoiMetrics {
    std::size_t operations = 0;
    double wall_ms = 0.0;
    double cpu_ms = 0.0;
    double operations_per_second = 0.0;
    double p50_us = 0.0;
    double p95_us = 0.0;
    double p99_us = 0.0;
    double average_aoi_recipients = 0.0;
    double full_broadcast_recipients = 0.0;
    double recipient_reduction_percent = 0.0;
};

double CpuMilliseconds() {
    return 1000.0 * static_cast<double>(std::clock()) /
           static_cast<double>(CLOCKS_PER_SEC);
}

double Milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

double PeakResidentMemoryMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return 0.0;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) /
           (1024.0 * 1024.0);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#endif
}

std::string OperatingSystem() {
#ifdef _WIN32
    return "Windows";
#else
    utsname info{};
    if (uname(&info) != 0) {
        return "Unix-like";
    }
    return std::string(info.sysname) + " " + info.release + " " +
           info.machine;
#endif
}

std::string Compiler() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return std::string("MSVC ") + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string UtcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string JsonEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (const char character : input) {
        switch (character) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += character;
                break;
        }
    }
    return output;
}

std::size_t ParseSize(const std::string& value, const char* name) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
    return static_cast<std::size_t>(parsed);
}

std::uint64_t ParseSeed(const std::string& value) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size()) {
        throw std::invalid_argument("invalid seed");
    }
    return parsed;
}

void PrintUsage() {
    std::cout
        << "Usage: mmo_bench [options]\n"
        << "  --players N             Number of simulated players (default 1000)\n"
        << "  --moves-per-player N    Movement operations per player (default 20)\n"
        << "  --repetitions N         Repeated runs (default 3)\n"
        << "  --payload-bytes N       Protocol payload bytes (default 32)\n"
        << "  --distribution NAME     uniform or hotspot (default uniform)\n"
        << "  --seed N                Deterministic random seed\n"
        << "  --output PATH           Write machine-readable JSON\n";
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            PrintUsage();
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + argument);
        }
        const std::string value = argv[++index];
        if (argument == "--players") {
            options.players = ParseSize(value, "player count");
        } else if (argument == "--moves-per-player") {
            options.moves_per_player = ParseSize(value, "move count");
        } else if (argument == "--repetitions") {
            options.repetitions = ParseSize(value, "repetition count");
        } else if (argument == "--payload-bytes") {
            options.payload_bytes = ParseSize(value, "payload size");
        } else if (argument == "--distribution") {
            options.distribution = value;
        } else if (argument == "--seed") {
            options.seed = ParseSeed(value);
        } else if (argument == "--output") {
            options.output_path = value;
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    if (options.players < 2) {
        throw std::invalid_argument("at least two players are required");
    }
    if (options.distribution != "uniform" &&
        options.distribution != "hotspot") {
        throw std::invalid_argument(
            "distribution must be uniform or hotspot");
    }
    if (options.payload_bytes > mmo::protocol::kDefaultMaxPayloadSize) {
        throw std::invalid_argument("payload exceeds protocol maximum");
    }
    return options;
}

double Percentile(std::vector<double> samples, double percentile) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const double rank = percentile * static_cast<double>(samples.size());
    const auto index = static_cast<std::size_t>(std::ceil(rank)) - 1;
    return samples[std::min(index, samples.size() - 1)];
}

ProtocolMetrics RunProtocolBenchmark(const Options& options) {
    ProtocolMetrics metrics;
    metrics.frames = options.players * options.moves_per_player *
                     options.repetitions;
    std::vector<std::uint8_t> payload(options.payload_bytes, 0x5a);
    std::vector<std::uint8_t> wire;
    wire.reserve(metrics.frames *
                 (mmo::protocol::kFrameHeaderSize + options.payload_bytes));

    const double encode_cpu_start = CpuMilliseconds();
    const auto encode_start = Clock::now();
    for (std::size_t index = 0; index < metrics.frames; ++index) {
        payload[index % payload.size()] =
            static_cast<std::uint8_t>(index & 0xffU);
        const auto encoded = EncodeFrame(MessageType::kMoveRequest, payload);
        wire.insert(wire.end(), encoded.begin(), encoded.end());
    }
    metrics.encode_wall_ms = Milliseconds(Clock::now() - encode_start);
    metrics.encode_cpu_ms = CpuMilliseconds() - encode_cpu_start;
    metrics.bytes = wire.size();

    FrameDecoder decoder;
    Frame frame;
    std::mt19937_64 random(options.seed);
    std::uniform_int_distribution<std::size_t> chunk_size(1, 256);
    std::size_t offset = 0;
    std::size_t decoded_frames = 0;

    const double decode_cpu_start = CpuMilliseconds();
    const auto decode_start = Clock::now();
    while (offset < wire.size()) {
        const std::size_t size =
            std::min(chunk_size(random), wire.size() - offset);
        decoder.Append(wire.data() + offset, size);
        offset += size;

        while (true) {
            const auto status = decoder.Next(frame);
            if (status == DecodeStatus::kFrameReady) {
                ++decoded_frames;
                continue;
            }
            if (status == DecodeStatus::kNeedMoreData) {
                break;
            }
            throw std::runtime_error("protocol decoder failed during benchmark");
        }
    }
    metrics.decode_wall_ms = Milliseconds(Clock::now() - decode_start);
    metrics.decode_cpu_ms = CpuMilliseconds() - decode_cpu_start;

    if (decoded_frames != metrics.frames || decoder.BufferedBytes() != 0) {
        throw std::runtime_error("decoded frame count does not match input");
    }

    metrics.encode_frames_per_second =
        static_cast<double>(metrics.frames) * 1000.0 /
        metrics.encode_wall_ms;
    metrics.decode_frames_per_second =
        static_cast<double>(metrics.frames) * 1000.0 /
        metrics.decode_wall_ms;
    return metrics;
}

double Clamp(double value, double minimum, double maximum) {
    return std::max(minimum, std::min(value, maximum));
}

AoiMetrics RunAoiBenchmark(const Options& options) {
    constexpr double kMapWidth = 1000.0;
    constexpr double kMapHeight = 2000.0;
    constexpr std::size_t kColumns = 10;
    constexpr std::size_t kRows = 20;
    constexpr double kHotspotMinX = 400.0;
    constexpr double kHotspotMaxX = 600.0;
    constexpr double kHotspotMinY = 800.0;
    constexpr double kHotspotMaxY = 1200.0;
    constexpr double kMoveRadius = 75.0;
    constexpr double kInsideMaximumOffset = 0.000001;

    AoiMetrics metrics;
    metrics.operations = options.players * options.moves_per_player *
                         options.repetitions;
    metrics.full_broadcast_recipients =
        static_cast<double>(options.players - 1);
    std::vector<double> latency_samples;
    latency_samples.reserve(metrics.operations);
    std::uint64_t total_recipients = 0;
    double total_wall_ms = 0.0;
    double total_cpu_ms = 0.0;

    for (std::size_t repetition = 0;
         repetition < options.repetitions;
         ++repetition) {
        AoiWorld world(AoiConfig{
            0.0, kMapWidth, 0.0, kMapHeight, kColumns, kRows});
        std::mt19937_64 random(options.seed + repetition);
        std::uniform_real_distribution<double> uniform_x(
            0.0, kMapWidth - kInsideMaximumOffset);
        std::uniform_real_distribution<double> uniform_y(
            0.0, kMapHeight - kInsideMaximumOffset);
        std::uniform_real_distribution<double> hotspot_x(
            kHotspotMinX, kHotspotMaxX - kInsideMaximumOffset);
        std::uniform_real_distribution<double> hotspot_y(
            kHotspotMinY, kHotspotMaxY - kInsideMaximumOffset);
        std::uniform_real_distribution<double> probability(0.0, 1.0);
        std::uniform_real_distribution<double> movement(
            -kMoveRadius, kMoveRadius);

        std::vector<Position> positions(options.players);
        std::vector<bool> hotspot_players(options.players, false);
        for (std::size_t index = 0; index < options.players; ++index) {
            const bool use_hotspot =
                options.distribution == "hotspot" &&
                probability(random) < 0.8;
            hotspot_players[index] = use_hotspot;
            positions[index] = use_hotspot
                                   ? Position{hotspot_x(random), hotspot_y(random)}
                                   : Position{uniform_x(random), uniform_y(random)};
            if (!world.AddPlayer(index + 1, positions[index])) {
                throw std::runtime_error("failed to initialize AOI player");
            }
        }

        const double cpu_start = CpuMilliseconds();
        const auto wall_start = Clock::now();
        const std::size_t operations =
            options.players * options.moves_per_player;
        for (std::size_t operation = 0; operation < operations; ++operation) {
            const std::size_t player_index = operation % options.players;
            const double min_x = hotspot_players[player_index]
                                     ? kHotspotMinX
                                     : 0.0;
            const double max_x = hotspot_players[player_index]
                                     ? kHotspotMaxX
                                     : kMapWidth;
            const double min_y = hotspot_players[player_index]
                                     ? kHotspotMinY
                                     : 0.0;
            const double max_y = hotspot_players[player_index]
                                     ? kHotspotMaxY
                                     : kMapHeight;
            Position next{
                Clamp(
                    positions[player_index].x + movement(random),
                    min_x,
                    max_x - kInsideMaximumOffset),
                Clamp(
                    positions[player_index].y + movement(random),
                    min_y,
                    max_y - kInsideMaximumOffset),
            };

            const auto operation_start = Clock::now();
            const auto delta = world.MovePlayer(player_index + 1, next);
            const auto operation_end = Clock::now();
            if (!delta.has_value()) {
                throw std::runtime_error("valid AOI move was rejected");
            }
            latency_samples.push_back(
                std::chrono::duration<double, std::micro>(
                    operation_end - operation_start)
                    .count());
            total_recipients += delta->entered.size() + delta->stayed.size();
            positions[player_index] = next;
        }
        total_wall_ms += Milliseconds(Clock::now() - wall_start);
        total_cpu_ms += CpuMilliseconds() - cpu_start;
    }

    metrics.wall_ms = total_wall_ms;
    metrics.cpu_ms = total_cpu_ms;
    metrics.operations_per_second =
        static_cast<double>(metrics.operations) * 1000.0 /
        metrics.wall_ms;
    metrics.p50_us = Percentile(latency_samples, 0.50);
    metrics.p95_us = Percentile(latency_samples, 0.95);
    metrics.p99_us = Percentile(latency_samples, 0.99);
    metrics.average_aoi_recipients =
        static_cast<double>(total_recipients) /
        static_cast<double>(metrics.operations);
    metrics.recipient_reduction_percent =
        (1.0 - metrics.average_aoi_recipients /
                   metrics.full_broadcast_recipients) *
        100.0;
    return metrics;
}

std::string BuildJson(
    const Options& options,
    const ProtocolMetrics& protocol,
    const AoiMetrics& aoi,
    double peak_rss_mb) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"benchmark_kind\": \"in_process_module_benchmark\",\n"
           << "  \"warning\": \"Not an end-to-end epoll server or network latency benchmark\",\n"
           << "  \"timestamp_utc\": \"" << UtcTimestamp() << "\",\n"
           << "  \"environment\": {\n"
           << "    \"os\": \"" << JsonEscape(OperatingSystem()) << "\",\n"
           << "    \"compiler\": \"" << JsonEscape(Compiler()) << "\",\n"
           << "    \"hardware_concurrency\": "
           << std::thread::hardware_concurrency() << ",\n"
           << "    \"peak_rss_mb\": " << peak_rss_mb << "\n"
           << "  },\n"
           << "  \"configuration\": {\n"
           << "    \"players\": " << options.players << ",\n"
           << "    \"moves_per_player\": " << options.moves_per_player << ",\n"
           << "    \"repetitions\": " << options.repetitions << ",\n"
           << "    \"payload_bytes\": " << options.payload_bytes << ",\n"
           << "    \"distribution\": \"" << options.distribution << "\",\n"
           << "    \"seed\": " << options.seed << "\n"
           << "  },\n"
           << "  \"protocol\": {\n"
           << "    \"frames\": " << protocol.frames << ",\n"
           << "    \"wire_bytes\": " << protocol.bytes << ",\n"
           << "    \"encode_wall_ms\": " << protocol.encode_wall_ms << ",\n"
           << "    \"encode_cpu_ms\": " << protocol.encode_cpu_ms << ",\n"
           << "    \"decode_wall_ms\": " << protocol.decode_wall_ms << ",\n"
           << "    \"decode_cpu_ms\": " << protocol.decode_cpu_ms << ",\n"
           << "    \"encode_frames_per_second\": "
           << protocol.encode_frames_per_second << ",\n"
           << "    \"decode_frames_per_second\": "
           << protocol.decode_frames_per_second << "\n"
           << "  },\n"
           << "  \"aoi\": {\n"
           << "    \"operations\": " << aoi.operations << ",\n"
           << "    \"wall_ms\": " << aoi.wall_ms << ",\n"
           << "    \"cpu_ms\": " << aoi.cpu_ms << ",\n"
           << "    \"operations_per_second\": "
           << aoi.operations_per_second << ",\n"
           << "    \"p50_us\": " << aoi.p50_us << ",\n"
           << "    \"p95_us\": " << aoi.p95_us << ",\n"
           << "    \"p99_us\": " << aoi.p99_us << ",\n"
           << "    \"average_aoi_recipients\": "
           << aoi.average_aoi_recipients << ",\n"
           << "    \"full_broadcast_recipients\": "
           << aoi.full_broadcast_recipients << ",\n"
           << "    \"recipient_reduction_percent\": "
           << aoi.recipient_reduction_percent << "\n"
           << "  }\n"
           << "}\n";
    return output.str();
}

void PrintSummary(
    const Options& options,
    const ProtocolMetrics& protocol,
    const AoiMetrics& aoi,
    double peak_rss_mb) {
    std::cout << std::fixed << std::setprecision(3)
              << "Benchmark kind: in-process module benchmark\n"
              << "Players: " << options.players
              << ", distribution: " << options.distribution << '\n'
              << "Protocol encode: " << protocol.encode_frames_per_second
              << " frames/s\n"
              << "Protocol decode: " << protocol.decode_frames_per_second
              << " frames/s\n"
              << "AOI moves: " << aoi.operations_per_second << " ops/s\n"
              << "AOI p99: " << aoi.p99_us << " us\n"
              << "Average recipients: " << aoi.average_aoi_recipients
              << " vs " << aoi.full_broadcast_recipients << '\n'
              << "Recipient reduction: "
              << aoi.recipient_reduction_percent << "%\n"
              << "Peak RSS: " << peak_rss_mb << " MB\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = ParseOptions(argc, argv);
        const auto protocol = RunProtocolBenchmark(options);
        const auto aoi = RunAoiBenchmark(options);
        const double peak_rss_mb = PeakResidentMemoryMb();
        const std::string json =
            BuildJson(options, protocol, aoi, peak_rss_mb);

        PrintSummary(options, protocol, aoi, peak_rss_mb);
        if (!options.output_path.empty()) {
            std::ofstream output(options.output_path);
            if (!output) {
                throw std::runtime_error(
                    "failed to open output file: " + options.output_path);
            }
            output << json;
            std::cout << "JSON: " << options.output_path << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mmo_bench: " << error.what() << '\n';
        return 1;
    }
}

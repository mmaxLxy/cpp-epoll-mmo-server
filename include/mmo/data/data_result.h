#pragma once

#include <optional>
#include <string>
#include <utility>

namespace mmo::data {

enum class DataStatus {
    kOk,
    kNotFound,
    kInvalidArgument,
    kUnavailable,
};

struct OperationResult {
    DataStatus status = DataStatus::kOk;
    std::string error;

    bool Ok() const noexcept { return status == DataStatus::kOk; }

    static OperationResult Success() { return {}; }

    static OperationResult Failure(DataStatus status, std::string error) {
        return OperationResult{status, std::move(error)};
    }
};

template <typename T>
struct DataResult {
    DataStatus status = DataStatus::kNotFound;
    std::optional<T> value;
    std::string error;

    bool Ok() const noexcept {
        return status == DataStatus::kOk && value.has_value();
    }

    static DataResult Success(T value) {
        return DataResult{DataStatus::kOk, std::move(value), {}};
    }

    static DataResult NotFound() {
        return DataResult{DataStatus::kNotFound, std::nullopt, {}};
    }

    static DataResult Failure(DataStatus status, std::string error) {
        return DataResult{status, std::nullopt, std::move(error)};
    }
};

}  // namespace mmo::data

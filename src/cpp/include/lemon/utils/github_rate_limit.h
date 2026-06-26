#pragma once

#include "http_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace lemon {
namespace utils {

namespace detail {

inline std::string trim_header_value(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline std::string lowercase_header_name(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::optional<std::string> find_header_case_insensitive(
    const std::map<std::string, std::string>& headers,
    const std::string& name) {
    const std::string wanted = lowercase_header_name(name);
    for (const auto& header : headers) {
        if (lowercase_header_name(header.first) == wanted) {
            return trim_header_value(header.second);
        }
    }
    return std::nullopt;
}

inline std::optional<long long> parse_non_negative_integer(const std::string& value) {
    const std::string trimmed = trim_header_value(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    long long parsed = 0;
    for (char ch : trimmed) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        parsed = (parsed * 10) + (ch - '0');
    }
    return parsed;
}

} // namespace detail

inline std::optional<std::chrono::system_clock::time_point> github_rate_limit_backoff_until(
    const HttpResponse& response,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
    if (response.status_code != 403 && response.status_code != 429) {
        return std::nullopt;
    }

    if (auto retry_after = detail::find_header_case_insensitive(response.headers, "retry-after")) {
        if (auto seconds = detail::parse_non_negative_integer(*retry_after)) {
            return now + std::chrono::seconds(*seconds);
        }
    }

    const auto remaining = detail::find_header_case_insensitive(response.headers, "x-ratelimit-remaining");
    const bool rate_limit_exhausted = remaining.has_value() && *remaining == "0";

    if (rate_limit_exhausted) {
        if (auto reset = detail::find_header_case_insensitive(response.headers, "x-ratelimit-reset")) {
            if (auto epoch_seconds = detail::parse_non_negative_integer(*reset)) {
                return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(*epoch_seconds));
            }
        }
    }

    if (response.status_code == 429 || rate_limit_exhausted) {
        return now + std::chrono::seconds(60);
    }

    return std::nullopt;
}

class GitHubRateLimitBackoff {
public:
    std::optional<std::chrono::system_clock::time_point> backoff_until(
        const std::string& key,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backoff_until_by_key_.find(key);
        if (it == backoff_until_by_key_.end()) {
            return std::nullopt;
        }
        if (now >= it->second) {
            backoff_until_by_key_.erase(it);
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::chrono::system_clock::time_point> record_response(
        const std::string& key,
        const HttpResponse& response,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
        auto until = github_rate_limit_backoff_until(response, now);
        if (!until.has_value()) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        backoff_until_by_key_[key] = *until;
        return until;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> backoff_until_by_key_;
};

} // namespace utils
} // namespace lemon

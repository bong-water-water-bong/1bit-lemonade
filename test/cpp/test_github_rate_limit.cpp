#include "lemon/utils/github_rate_limit.h"
#include "lemon/utils/http_client.h"

#include <cassert>
#include <chrono>
#include <ctime>
#include <iostream>

using lemon::utils::GitHubRateLimitBackoff;
using lemon::utils::HttpResponse;
using lemon::utils::github_rate_limit_backoff_until;

int main() {
    using clock = std::chrono::system_clock;
    const auto now = clock::from_time_t(1700000000);

    {
        HttpResponse response;
        response.status_code = 403;
        response.headers["x-ratelimit-remaining"] = "0";
        response.headers["x-ratelimit-reset"] = "1700000120";

        auto until = github_rate_limit_backoff_until(response, now);
        assert(until.has_value());
        assert(clock::to_time_t(*until) == 1700000120);
    }

    {
        HttpResponse response;
        response.status_code = 429;
        response.headers["Retry-After"] = "30";

        auto until = github_rate_limit_backoff_until(response, now);
        assert(until.has_value());
        assert(clock::to_time_t(*until) == 1700000030);
    }

    {
        HttpResponse response;
        response.status_code = 403;
        response.headers["retry-after"] = "45";
        response.headers["x-ratelimit-remaining"] = "0";
        response.headers["x-ratelimit-reset"] = "1700000120";

        auto until = github_rate_limit_backoff_until(response, now);
        assert(until.has_value());
        assert(clock::to_time_t(*until) == 1700000045);
    }

    {
        HttpResponse response;
        response.status_code = 429;

        auto until = github_rate_limit_backoff_until(response, now);
        assert(until.has_value());
        assert(clock::to_time_t(*until) == 1700000060);
    }

    {
        HttpResponse response;
        response.status_code = 403;
        response.headers["x-ratelimit-remaining"] = "0";
        response.headers["x-ratelimit-reset"] = "not-a-timestamp";

        auto until = github_rate_limit_backoff_until(response, now);
        assert(until.has_value());
        assert(clock::to_time_t(*until) == 1700000060);
    }

    {
        HttpResponse response;
        response.status_code = 404;
        response.headers["x-ratelimit-remaining"] = "0";
        response.headers["x-ratelimit-reset"] = "1700000120";

        auto until = github_rate_limit_backoff_until(response, now);
        assert(!until.has_value());
    }

    {
        GitHubRateLimitBackoff backoff;
        HttpResponse response;
        response.status_code = 403;
        response.headers["x-ratelimit-remaining"] = "0";
        response.headers["x-ratelimit-reset"] = "1700000120";

        assert(!backoff.backoff_until("ggml-org/llama.cpp", now).has_value());
        auto recorded = backoff.record_response("ggml-org/llama.cpp", response, now);
        assert(recorded.has_value());
        assert(clock::to_time_t(*recorded) == 1700000120);

        auto active = backoff.backoff_until("ggml-org/llama.cpp", now + std::chrono::seconds(30));
        assert(active.has_value());
        assert(clock::to_time_t(*active) == 1700000120);

        auto expired = backoff.backoff_until("ggml-org/llama.cpp", now + std::chrono::seconds(121));
        assert(!expired.has_value());

        HttpResponse ok_response;
        ok_response.status_code = 200;
        auto ignored = backoff.record_response("ggml-org/llama.cpp", ok_response, now);
        assert(!ignored.has_value());
        assert(!backoff.backoff_until("ggml-org/llama.cpp", now).has_value());
    }

    std::cout << "GitHub rate-limit helper tests passed\n";
    return 0;
}
